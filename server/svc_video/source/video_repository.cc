#include "video_repository.h"

#include "../../common/outbox.h"
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
#include "message.pb.h"
#endif

#include "../../common/elasticsearch.h"

#include "../../common/util.h"
#include "../../common/session_token.h"

#include <filesystem>

namespace bitevideo {
namespace {

constexpr std::size_t VIDEO_FIELD_COUNT = 10;
const std::string VIDEO_SELECT =
    "SELECT video_id, title, user_name, "
    "DATE_FORMAT(published_on, '%c-%e'), duration_seconds, "
    "CAST(play_count AS CHAR), CAST(like_count AS CHAR), category, "
    "CAST(tags AS CHAR), description FROM videos ";
const std::string PUBLIC_VIDEO_PREDICATE =
    "status = 1 AND review_status = '审核通过' "
    "AND transcode_status = 'READY'";

bool isLocalUploadPath(const std::string &path) {
    if (path.empty())
        return false;
    const std::filesystem::path normalized =
        std::filesystem::path(path).lexically_normal();
    if (normalized.is_absolute())
        return false;
    const auto first = normalized.begin();
    return first != normalized.end() && *first == "uploads" &&
           normalized.string().find("..") == std::string::npos;
}

std::string valueOrEmpty(const std::optional<std::string> &value) {
    return value.value_or("");
}

bool videoFromRow(const bitedb::Database::QueryRow &row, Video &video,
                  std::string &error) {
    if (row.size() != VIDEO_FIELD_COUNT) {
        error = "视频查询返回了不符合预期的字段数量";
        return false;
    }

    video.id = valueOrEmpty(row[0]);
    video.title = valueOrEmpty(row[1]);
    video.userName = valueOrEmpty(row[2]);
    video.date = valueOrEmpty(row[3]);
    try {
        video.durationSeconds =
            static_cast<std::size_t>(std::stoull(valueOrEmpty(row[4])));
    } catch (const std::exception &) {
        error = "视频时长不是有效数字: " + video.id;
        return false;
    }
    video.playCount = valueOrEmpty(row[5]);
    video.likeCount = valueOrEmpty(row[6]);
    video.category = valueOrEmpty(row[7]);
    video.description = valueOrEmpty(row[9]);

    const auto tags = biteutil::JSON::unserialize(valueOrEmpty(row[8]));
    if (!tags || !tags->isArray()) {
        error = "视频标签不是有效JSON数组: " + video.id;
        return false;
    }
    for (const Json::Value &tag : *tags) {
        if (tag.isString()) {
            video.tags.push_back(tag.asString());
        }
    }
    return true;
}

bool publicVideoExists(bitedb::Database &database,
                       const std::string &escapedVideoId, bool &exists,
                       std::string &error) {
    exists = false;
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = "SELECT 1 FROM videos WHERE " +
                            PUBLIC_VIDEO_PREDICATE + " AND video_id = '" +
                            escapedVideoId + "' LIMIT 1";
    if (!database.query(sql, rows, error)) {
        return false;
    }
    exists = !rows.empty();
    return true;
}

bool commentFromRow(const bitedb::Database::QueryRow &row,
                    VideoComment &comment, std::string &error) {
    if (row.size() != 6) {
        error = "评论查询返回了不符合预期的字段数量";
        return false;
    }
    comment.id = "comment-" + valueOrEmpty(row[0]);
    comment.videoId = valueOrEmpty(row[1]);
    comment.userName = valueOrEmpty(row[2]);
    comment.account = valueOrEmpty(row[3]);
    comment.content = valueOrEmpty(row[4]);
    comment.createdAt = valueOrEmpty(row[5]);
    return true;
}

bool barrageFromRow(const bitedb::Database::QueryRow &row,
                    VideoBarrage &barrage, std::string &error) {
    if (row.size() != 2) {
        error = "弹幕查询返回了不符合预期的字段数量";
        return false;
    }
    try {
        barrage.seconds = std::stoi(valueOrEmpty(row[0]));
    } catch (const std::exception &) {
        error = "弹幕时间不是有效数字";
        return false;
    }
    barrage.text = valueOrEmpty(row[1]);
    return true;
}

bool profileFromRow(const bitedb::Database::QueryRow &row, UserProfile &profile,
                    std::string &error) {
    if (row.size() != 4) {
        error = "用户资料查询返回了不符合预期的字段数量";
        return false;
    }
    profile.account = valueOrEmpty(row[0]);
    profile.userName = valueOrEmpty(row[1]);
    profile.description = valueOrEmpty(row[2]);
    profile.avatarPath = valueOrEmpty(row[3]);
    return true;
}

} // namespace

MySqlVideoRepository::MySqlVideoRepository(
    bitedb::Database &database, bitesearch::IVideoSearchIndex *searchIndex,
    biteevent::MySqlOutboxRepository *outbox)
    : database_(database), searchIndex_(searchIndex), outbox_(outbox) {}

bool MySqlVideoRepository::list(std::vector<Video> &videos,
                                std::string &error) {
    videos.clear();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = VIDEO_SELECT + "WHERE " + PUBLIC_VIDEO_PREDICATE +
                            " ORDER BY published_on DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto &row : rows) {
        Video video;
        if (!videoFromRow(row, video, error)) {
            videos.clear();
            return false;
        }
        videos.push_back(std::move(video));
    }
    return true;
}

bool MySqlVideoRepository::createVideo(const VideoDraft &draft,
                                       std::optional<Video> &video,
                                       std::string &error) {
    video.reset();
    std::string randomToken;
    if (!bitesession::generateSessionToken(randomToken, error))
        return false;
    // 128 random bits keeps the existing VARCHAR(64) business key compact
    // while removing the MAX(id)+1 race between concurrent uploads.
    const std::string videoId = "video-" + randomToken.substr(4, 32);
    const std::string sourcePath =
        draft.playUrl.empty() ? draft.videoFileName : draft.playUrl;
    const bool needsTranscode =
        isLocalUploadPath(sourcePath) || sourcePath.rfind("object:", 0) == 0;
    Json::Value tagArray(Json::arrayValue);
    for (const auto &tag : draft.tags) {
        tagArray.append(tag);
    }
    const auto tagsJson = biteutil::JSON::serialize(tagArray);
    if (!tagsJson) {
        error = "视频标签序列化失败";
        return false;
    }

    std::string escapedVideoId;
    std::string escapedTitle;
    std::string escapedUserName;
    std::string escapedAccount;
    std::string escapedCategory;
    std::string escapedTags;
    std::string escapedDescription;
    std::string escapedPlayUrl;
    std::string escapedVideoFileName;
    std::string escapedCoverFileName;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(draft.title, escapedTitle, error) ||
        !database_.escape(draft.userName, escapedUserName, error) ||
        !database_.escape(draft.account, escapedAccount, error) ||
        !database_.escape(draft.category, escapedCategory, error) ||
        !database_.escape(*tagsJson, escapedTags, error) ||
        !database_.escape(draft.description, escapedDescription, error) ||
        !database_.escape(sourcePath, escapedPlayUrl, error) ||
        !database_.escape(draft.videoFileName, escapedVideoFileName, error) ||
        !database_.escape(draft.coverFileName, escapedCoverFileName, error)) {
        return false;
    }

    std::string escapedCoverPath;
    if (!database_.escape(draft.coverPath, escapedCoverPath, error))
        return false;
    const std::string sql =
        "INSERT INTO videos (video_id, title, user_name, owner_account, "
        "published_on, duration_seconds, play_count, like_count, category, "
        "tags, description, play_url, video_file_name, cover_file_name, "
        "status, review_status, cover_path, transcode_status) VALUES ('" +
        escapedVideoId + "', '" + escapedTitle + "', '" + escapedUserName +
        "', '" + escapedAccount + "', CURDATE(), 0, 0, 0, '" + escapedCategory +
        "', '" + escapedTags + "', '" + escapedDescription + "', '" +
        escapedPlayUrl + "', '" + escapedVideoFileName + "', '" +
        escapedCoverFileName + "', 1, '待审核', '" + escapedCoverPath + "', '" +
        (needsTranscode ? "PENDING" : "READY") + "')";
    if (needsTranscode) {
        std::string escapedOutput;
        const std::string outputPath =
            "uploads/transcoded/" + videoId +
            (sourcePath.rfind("object:", 0) == 0 ? "/index.m3u8" : ".mp4");
        if (!database_.escape(outputPath, escapedOutput, error))
            return false;
        const std::string jobSql =
            "INSERT INTO transcode_jobs (job_id, video_id, owner_account, "
            "input_path, output_path, status, attempts, max_attempts, "
            "next_attempt_at) VALUES ('transcode-" +
            escapedVideoId + "', '" + escapedVideoId + "', '" + escapedAccount +
            "', '" + escapedPlayUrl + "', '" + escapedOutput +
            "', 'PENDING', 0, 3, NOW())";
        std::vector<std::string> transaction{sql, jobSql};
#ifndef VOD_ENABLE_REFERENCE_RUNTIME
        if (outbox_) {
            error = "outbox requires reference runtime";
            return false;
        }
#else
        if (outbox_) {
            vod::api::HlsTranscodeMessage message;
            message.set_video_id(videoId);
            message.set_source_file(sourcePath);
            message.set_output_bundle_id("bundle-" + videoId);
            std::string payload;
            if (!message.SerializeToString(&payload)) {
                error = "failed to serialize transcode message";
                return false;
            }
            biteevent::OutboxEvent event;
            event.eventId = biteutil::Random::code(32);
            event.exchange = "vod.transcode";
            event.routingKey = "transcode.hls";
            event.eventType = "HLS_TRANSCODE_REQUESTED";
            event.aggregateType = "video";
            event.aggregateId = videoId;
            vod::api::EventEnvelope envelope;
            envelope.set_event_id(event.eventId);
            envelope.set_kind(vod::api::HLS_TRANSCODE_REQUESTED);
            envelope.set_aggregate_type("video");
            envelope.set_aggregate_id(videoId);
            envelope.set_payload(payload);
            if (!envelope.SerializeToString(&event.payload)) {
                error = "failed to serialize transcode event";
                return false;
            }
            std::string outboxSql;
            if (!outbox_->buildInsertSql(event, outboxSql, error))
                return false;
            transaction.push_back(std::move(outboxSql));
        }
#endif
        if (!database_.executeTransaction(transaction, error))
            return false;
    } else if (!database_.execute(sql, error)) {
        return false;
    }

    return findAnyById(videoId, video, error);
}

bool MySqlVideoRepository::findById(const std::string &videoId,
                                    std::optional<Video> &video,
                                    std::string &error) {
    video.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = VIDEO_SELECT + "WHERE " + PUBLIC_VIDEO_PREDICATE +
                            " AND video_id = '" + escapedVideoId + "' LIMIT 1";
    if (!database_.query(sql, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        return true;
    }

    Video found;
    if (!videoFromRow(rows.front(), found, error)) {
        return false;
    }
    video = std::move(found);
    return true;
}

bool MySqlVideoRepository::findAnyById(const std::string &videoId,
                                       std::optional<Video> &video,
                                       std::string &error) {
    video.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        VIDEO_SELECT + "WHERE video_id = '" + escapedVideoId + "' LIMIT 1";
    if (!database_.query(sql, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        return true;
    }
    Video found;
    if (!videoFromRow(rows.front(), found, error)) {
        return false;
    }
    video = std::move(found);
    return true;
}

bool MySqlVideoRepository::search(const std::string &keyword,
                                  std::vector<Video> &videos,
                                  std::string &error) {
    videos.clear();
    if (searchIndex_) {
        std::vector<std::string> ids;
        if (!searchIndex_->searchIds(keyword, ids, error)) {
            error = "elasticsearch unavailable: " + error;
            return false;
        }
        for (const auto &id : ids) {
            std::optional<Video> video;
            if (!findById(id, video, error))
                return false;
            if (video)
                videos.push_back(std::move(*video));
        }
        return true;
    }
    std::string escapedKeyword;
    if (!database_.escape(keyword, escapedKeyword, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string pattern = "'%" + escapedKeyword + "%'";
    const std::string sql =
        VIDEO_SELECT + "WHERE " + PUBLIC_VIDEO_PREDICATE + " AND (title LIKE " +
        pattern + " OR user_name LIKE " + pattern + " OR category LIKE " +
        pattern + " OR CAST(tags AS CHAR) LIKE " + pattern +
        " OR description LIKE " + pattern +
        ") ORDER BY published_on DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto &row : rows) {
        Video video;
        if (!videoFromRow(row, video, error)) {
            videos.clear();
            return false;
        }
        videos.push_back(std::move(video));
    }
    return true;
}

bool MySqlVideoRepository::playUrl(const std::string &videoId,
                                   std::optional<std::string> &url,
                                   std::string &error) {
    url.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = "SELECT play_url FROM videos WHERE " +
                            PUBLIC_VIDEO_PREDICATE + " AND video_id = '" +
                            escapedVideoId + "' LIMIT 1";
    if (!database_.query(sql, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        return true;
    }
    if (rows.front().size() != 1) {
        error = "播放地址查询返回了不符合预期的字段数量";
        return false;
    }

    url = valueOrEmpty(rows.front()[0]);
    return true;
}

bool MySqlVideoRepository::likeStatus(const std::string &videoId,
                                      const std::string &account,
                                      std::optional<LikeStatus> &status,
                                      std::string &error) {
    status.reset();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT EXISTS(SELECT 1 FROM video_likes vl "
        "WHERE vl.video_id = v.video_id AND vl.account = ?), "
        "CAST(v.like_count AS CHAR) FROM videos v WHERE v.video_id = ? "
        "AND v.status = 1 AND v.review_status = '审核通过' "
        "AND v.transcode_status = 'READY' LIMIT 1";
    if (!database_.queryPrepared(sql, {account, videoId}, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        return true;
    }
    if (rows.front().size() != 2) {
        error = "点赞状态查询返回了不符合预期的字段数量";
        return false;
    }

    status = LikeStatus{valueOrEmpty(rows.front()[0]) == "1",
                        valueOrEmpty(rows.front()[1])};
    return true;
}

bool MySqlVideoRepository::setLiked(const std::string &videoId,
                                    const std::string &account, bool shouldLike,
                                    std::optional<LikeStatus> &status,
                                    std::string &error) {
    if (!likeStatus(videoId, account, status, error)) {
        return false;
    }
    if (!status) {
        return true;
    }

    const std::string changeSql =
        shouldLike
            ? "INSERT IGNORE INTO video_likes (video_id, account) VALUES (?, ?)"
            : "DELETE FROM video_likes WHERE video_id = ? AND account = ?";
    const std::string followupSql =
        shouldLike
            ? "UPDATE videos SET like_count = like_count + 1 WHERE video_id = ?"
            : "UPDATE videos SET like_count = GREATEST(like_count - 1, 0) "
              "WHERE video_id = ?";
    bool changed = false;
    if (!database_.executeIfChangedPrepared(changeSql, {videoId, account},
                                            followupSql, {videoId}, changed,
                                            error)) {
        return false;
    }
    return likeStatus(videoId, account, status, error);
}

bool MySqlVideoRepository::watchProgress(const std::string &videoId,
                                         const std::string &account,
                                         std::optional<WatchProgress> &progress,
                                         std::string &error) {
    progress.reset();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = "SELECT COALESCE(wp.seconds, 0) FROM videos v "
                            "LEFT JOIN video_watch_progress wp "
                            "ON wp.video_id = v.video_id AND wp.account = ? "
                            "WHERE v.video_id = ? AND v.status = 1 "
                            "AND v.review_status = '审核通过' "
                            "AND v.transcode_status = 'READY' LIMIT 1";
    if (!database_.queryPrepared(sql, {account, videoId}, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        return true;
    }
    if (rows.front().size() != 1) {
        error = "播放进度查询返回了不符合预期的字段数量";
        return false;
    }

    try {
        progress = WatchProgress{std::stoi(valueOrEmpty(rows.front()[0]))};
    } catch (const std::exception &) {
        error = "播放进度不是有效数字";
        return false;
    }
    return true;
}

bool MySqlVideoRepository::saveWatchProgress(
    const std::string &videoId, const std::string &account, int seconds,
    std::optional<WatchProgress> &progress, std::string &error) {
    if (!watchProgress(videoId, account, progress, error)) {
        return false;
    }
    if (!progress) {
        return true;
    }

    const std::string sql =
        "INSERT INTO video_watch_progress (video_id, account, seconds) "
        "VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE seconds = VALUES(seconds)";
    if (!database_.executePrepared(
            sql, {videoId, account, std::to_string(seconds)}, error)) {
        return false;
    }
    return watchProgress(videoId, account, progress, error);
}

bool MySqlVideoRepository::favoriteStatus(const std::string &videoId,
                                          const std::string &account,
                                          std::optional<FavoriteStatus> &status,
                                          std::string &error) {
    status.reset();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT EXISTS(SELECT 1 FROM video_favorites vf "
        "WHERE vf.video_id = v.video_id AND vf.account = ?) "
        "FROM videos v WHERE v.video_id = ? AND v.status = 1 "
        "AND v.review_status = '审核通过' "
        "AND v.transcode_status = 'READY' LIMIT 1";
    if (!database_.queryPrepared(sql, {account, videoId}, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        return true;
    }
    if (rows.front().size() != 1) {
        error = "收藏状态查询返回了不符合预期的字段数量";
        return false;
    }

    status = FavoriteStatus{valueOrEmpty(rows.front()[0]) == "1"};
    return true;
}

bool MySqlVideoRepository::setFavorited(const std::string &videoId,
                                        const std::string &account,
                                        bool shouldFavorite,
                                        std::optional<FavoriteStatus> &status,
                                        std::string &error) {
    if (!favoriteStatus(videoId, account, status, error)) {
        return false;
    }
    if (!status) {
        return true;
    }

    const std::string sql =
        shouldFavorite
            ? "INSERT IGNORE INTO video_favorites (video_id, account) VALUES "
              "(?, ?)"
            : "DELETE FROM video_favorites WHERE video_id = ? AND account = ?";
    if (!database_.executePrepared(sql, {videoId, account}, error)) {
        return false;
    }
    return favoriteStatus(videoId, account, status, error);
}

bool MySqlVideoRepository::favoriteVideos(const std::string &account,
                                          std::vector<Video> &videos,
                                          std::string &error) {
    videos.clear();
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT videos.video_id, videos.title, videos.user_name, "
        "DATE_FORMAT(videos.published_on, '%c-%e'), videos.duration_seconds, "
        "CAST(videos.play_count AS CHAR), CAST(videos.like_count AS CHAR), "
        "videos.category, CAST(videos.tags AS CHAR), videos.description "
        "FROM videos "
        "INNER JOIN video_favorites vf ON vf.video_id = videos.video_id "
        "WHERE videos.status = 1 AND videos.review_status = '审核通过' "
        "AND videos.transcode_status = 'READY' "
        "AND vf.account = '" +
        escapedAccount + "' ORDER BY vf.created_at DESC, vf.id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto &row : rows) {
        Video video;
        if (!videoFromRow(row, video, error)) {
            videos.clear();
            return false;
        }
        videos.push_back(std::move(video));
    }
    return true;
}

bool MySqlVideoRepository::ownerVideos(const std::string &account,
                                       std::vector<Video> &videos,
                                       std::string &error) {
    videos.clear();
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        VIDEO_SELECT + "WHERE status = 1 AND owner_account = '" +
        escapedAccount + "' ORDER BY published_on DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto &row : rows) {
        Video video;
        if (!videoFromRow(row, video, error)) {
            videos.clear();
            return false;
        }
        videos.push_back(std::move(video));
    }
    return true;
}

bool MySqlVideoRepository::comments(
    const std::string &videoId,
    std::optional<std::vector<VideoComment>> &comments, std::string &error) {
    comments.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }

    bool exists = false;
    if (!publicVideoExists(database_, escapedVideoId, exists, error)) {
        return false;
    }
    if (!exists) {
        return true;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT LPAD(id, 3, '0'), video_id, user_name, account, content, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') FROM video_comments "
        "WHERE video_id = '" +
        escapedVideoId + "' ORDER BY created_at DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    std::vector<VideoComment> result;
    for (const auto &row : rows) {
        VideoComment comment;
        if (!commentFromRow(row, comment, error)) {
            return false;
        }
        result.push_back(std::move(comment));
    }
    comments = std::move(result);
    return true;
}

bool MySqlVideoRepository::addComment(const std::string &videoId,
                                      const std::string &userName,
                                      const std::string &account,
                                      const std::string &content,
                                      std::optional<VideoComment> &comment,
                                      std::string &error) {
    comment.reset();
    std::string escapedVideoId;
    std::string escapedUserName;
    std::string escapedAccount;
    std::string escapedContent;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(userName, escapedUserName, error) ||
        !database_.escape(account, escapedAccount, error) ||
        !database_.escape(content, escapedContent, error)) {
        return false;
    }

    bool exists = false;
    if (!publicVideoExists(database_, escapedVideoId, exists, error)) {
        return false;
    }
    if (!exists) {
        return true;
    }

    const std::string insertSql =
        "INSERT INTO video_comments (video_id, user_name, account, content) "
        "VALUES ('" +
        escapedVideoId + "', '" + escapedUserName + "', '" + escapedAccount +
        "', '" + escapedContent + "')";
    if (!database_.execute(insertSql, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string selectSql =
        "SELECT LPAD(id, 3, '0'), video_id, user_name, account, content, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') FROM video_comments "
        "WHERE id = LAST_INSERT_ID()";
    if (!database_.query(selectSql, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        error = "评论保存后无法读取";
        return false;
    }

    VideoComment saved;
    if (!commentFromRow(rows.front(), saved, error)) {
        return false;
    }
    comment = std::move(saved);
    return true;
}

bool MySqlVideoRepository::barrages(
    const std::string &videoId,
    std::optional<std::vector<VideoBarrage>> &barrages, std::string &error) {
    barrages.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }

    bool exists = false;
    if (!publicVideoExists(database_, escapedVideoId, exists, error)) {
        return false;
    }
    if (!exists) {
        return true;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT seconds, text FROM video_barrages WHERE video_id = '" +
        escapedVideoId + "' ORDER BY seconds ASC, id ASC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    std::vector<VideoBarrage> result;
    for (const auto &row : rows) {
        VideoBarrage barrage;
        if (!barrageFromRow(row, barrage, error)) {
            return false;
        }
        result.push_back(std::move(barrage));
    }
    barrages = std::move(result);
    return true;
}

bool MySqlVideoRepository::addBarrage(const std::string &videoId, int seconds,
                                      const std::string &text,
                                      std::optional<VideoBarrage> &barrage,
                                      std::string &error) {
    barrage.reset();
    std::string escapedVideoId;
    std::string escapedText;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(text, escapedText, error)) {
        return false;
    }

    bool exists = false;
    if (!publicVideoExists(database_, escapedVideoId, exists, error)) {
        return false;
    }
    if (!exists) {
        return true;
    }

    const std::string insertSql =
        "INSERT INTO video_barrages (video_id, seconds, text) VALUES ('" +
        escapedVideoId + "', " + std::to_string(seconds) + ", '" + escapedText +
        "')";
    if (!database_.execute(insertSql, error)) {
        return false;
    }
    barrage = VideoBarrage{seconds, text};
    return true;
}

bool MySqlVideoRepository::interactionUserProfile(
    const std::string &account, std::optional<UserProfile> &profile,
    std::string &error) {
    profile.reset();
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT account, user_name, description, avatar_path FROM users "
        "WHERE account = '" +
        escapedAccount + "' LIMIT 1";
    if (!database_.query(sql, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        return true;
    }

    UserProfile found;
    if (!profileFromRow(rows.front(), found, error)) {
        return false;
    }
    profile = std::move(found);
    return true;
}

} // namespace bitevideo
