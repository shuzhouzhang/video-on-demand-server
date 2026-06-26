#include "video_repository.h"

#include "util.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <regex>
#include <sstream>

namespace bitevideo {
namespace {

constexpr std::size_t VIDEO_FIELD_COUNT = 10;
const std::string VIDEO_SELECT =
    "SELECT video_id, title, user_name, "
    "DATE_FORMAT(published_on, '%c-%e'), duration_seconds, "
    "CAST(play_count AS CHAR), CAST(like_count AS CHAR), category, "
    "CAST(tags AS CHAR), description FROM videos ";

std::string valueOrEmpty(const std::optional<std::string>& value) {
    return value.value_or("");
}

bool videoFromRow(const bitedb::Database::QueryRow& row,
                  Video& video,
                  std::string& error) {
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
    } catch (const std::exception&) {
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
    for (const Json::Value& tag : *tags) {
        if (tag.isString()) {
            video.tags.push_back(tag.asString());
        }
    }
    return true;
}

bool videoExists(bitedb::Database& database,
                 const std::string& escapedVideoId,
                 bool& exists,
                 std::string& error) {
    exists = false;
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = "SELECT 1 FROM videos WHERE status = 1 "
        "AND video_id = '" + escapedVideoId + "' LIMIT 1";
    if (!database.query(sql, rows, error)) {
        return false;
    }
    exists = !rows.empty();
    return true;
}

bool commentFromRow(const bitedb::Database::QueryRow& row,
                    VideoComment& comment,
                    std::string& error) {
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

bool barrageFromRow(const bitedb::Database::QueryRow& row,
                    VideoBarrage& barrage,
                    std::string& error) {
    if (row.size() != 2) {
        error = "弹幕查询返回了不符合预期的字段数量";
        return false;
    }
    try {
        barrage.seconds = std::stoi(valueOrEmpty(row[0]));
    } catch (const std::exception&) {
        error = "弹幕时间不是有效数字";
        return false;
    }
    barrage.text = valueOrEmpty(row[1]);
    return true;
}

bool profileFromRow(const bitedb::Database::QueryRow& row,
                    UserProfile& profile,
                    std::string& error) {
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

bool reviewFromRow(const bitedb::Database::QueryRow& row,
                   AdminReview& review,
                   std::string& error) {
    if (row.size() != 5) {
        error = "审核列表查询返回了不符合预期的字段数量";
        return false;
    }
    review.videoId = valueOrEmpty(row[0]);
    review.title = valueOrEmpty(row[1]);
    review.userId = valueOrEmpty(row[2]);
    review.status = valueOrEmpty(row[3]);
    review.uploadTime = valueOrEmpty(row[4]);
    return true;
}

bool adminUserFromRow(const bitedb::Database::QueryRow& row,
                      AdminUser& user,
                      std::string& error) {
    if (row.size() != 5) {
        error = "后台用户列表查询返回了不符合预期的字段数量";
        return false;
    }
    user.account = valueOrEmpty(row[0]);
    user.userName = valueOrEmpty(row[1]);
    user.role = valueOrEmpty(row[2]);
    user.status = valueOrEmpty(row[3]);
    user.createdAt = valueOrEmpty(row[4]);
    return true;
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::string nameFromEmail(const std::string& email) {
    const auto at = email.find('@');
    if (at == std::string::npos || at == 0) {
        return email;
    }
    return email.substr(0, at);
}

std::string makeVideoId(unsigned long long nextId) {
    std::ostringstream out;
    out << "video-" << std::setw(3) << std::setfill('0') << nextId;
    return out.str();
}

}  // namespace

MySqlVideoRepository::MySqlVideoRepository(bitedb::Database& database)
    : database_(database) {}

bool MySqlVideoRepository::list(std::vector<Video>& videos,
                                std::string& error) {
    videos.clear();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = VIDEO_SELECT +
        "WHERE status = 1 ORDER BY published_on DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto& row : rows) {
        Video video;
        if (!videoFromRow(row, video, error)) {
            videos.clear();
            return false;
        }
        videos.push_back(std::move(video));
    }
    return true;
}

bool MySqlVideoRepository::createVideo(const VideoDraft& draft,
                                       std::optional<Video>& video,
                                       std::string& error) {
    video.reset();
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query("SELECT COALESCE(MAX(id), 0) + 1 FROM videos",
                         rows, error)) {
        return false;
    }
    if (rows.empty() || rows.front().empty()) {
        error = "无法生成视频编号";
        return false;
    }

    unsigned long long nextId = 0;
    try {
        nextId = std::stoull(valueOrEmpty(rows.front()[0]));
    } catch (const std::exception&) {
        error = "视频编号不是有效数字";
        return false;
    }

    const std::string videoId = makeVideoId(nextId);
    Json::Value tagArray(Json::arrayValue);
    for (const auto& tag : draft.tags) {
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
    std::string escapedVideoFileName;
    std::string escapedCoverFileName;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(draft.title, escapedTitle, error) ||
        !database_.escape(draft.userName, escapedUserName, error) ||
        !database_.escape(draft.account, escapedAccount, error) ||
        !database_.escape(draft.category, escapedCategory, error) ||
        !database_.escape(*tagsJson, escapedTags, error) ||
        !database_.escape(draft.description, escapedDescription, error) ||
        !database_.escape(draft.videoFileName, escapedVideoFileName, error) ||
        !database_.escape(draft.coverFileName, escapedCoverFileName, error)) {
        return false;
    }

    const std::string sql =
        "INSERT INTO videos (video_id, title, user_name, owner_account, "
        "published_on, duration_seconds, play_count, like_count, category, "
        "tags, description, play_url, video_file_name, cover_file_name, "
        "status, review_status) VALUES ('" + escapedVideoId + "', '" +
        escapedTitle + "', '" + escapedUserName + "', '" + escapedAccount +
        "', CURDATE(), 0, 0, 0, '" + escapedCategory + "', '" +
        escapedTags + "', '" + escapedDescription + "', '" +
        escapedVideoFileName + "', '" + escapedVideoFileName + "', '" +
        escapedCoverFileName + "', 1, '待审核')";
    if (!database_.execute(sql, error)) {
        return false;
    }

    return findById(videoId, video, error);
}

bool MySqlVideoRepository::findById(const std::string& videoId,
                                    std::optional<Video>& video,
                                    std::string& error) {
    video.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = VIDEO_SELECT + "WHERE status = 1 AND video_id = '" +
        escapedVideoId + "' LIMIT 1";
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

bool MySqlVideoRepository::search(const std::string& keyword,
                                  std::vector<Video>& videos,
                                  std::string& error) {
    videos.clear();
    std::string escapedKeyword;
    if (!database_.escape(keyword, escapedKeyword, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string pattern = "'%" + escapedKeyword + "%'";
    const std::string sql = VIDEO_SELECT +
        "WHERE status = 1 AND (title LIKE " + pattern +
        " OR user_name LIKE " + pattern +
        " OR category LIKE " + pattern +
        " OR CAST(tags AS CHAR) LIKE " + pattern +
        " OR description LIKE " + pattern +
        ") ORDER BY published_on DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto& row : rows) {
        Video video;
        if (!videoFromRow(row, video, error)) {
            videos.clear();
            return false;
        }
        videos.push_back(std::move(video));
    }
    return true;
}

bool MySqlVideoRepository::playUrl(const std::string& videoId,
                                   std::optional<std::string>& url,
                                   std::string& error) {
    url.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT play_url FROM videos WHERE status = 1 AND video_id = '" +
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

bool MySqlVideoRepository::likeStatus(
    const std::string& videoId,
    const std::string& account,
    std::optional<LikeStatus>& status,
    std::string& error) {
    status.reset();
    std::string escapedVideoId;
    std::string escapedAccount;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT EXISTS(SELECT 1 FROM video_likes vl "
        "WHERE vl.video_id = v.video_id AND vl.account = '" +
        escapedAccount + "'), CAST(v.like_count AS CHAR) "
        "FROM videos v WHERE v.video_id = '" + escapedVideoId +
        "' AND v.status = 1 LIMIT 1";
    if (!database_.query(sql, rows, error)) {
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

bool MySqlVideoRepository::setLiked(
    const std::string& videoId,
    const std::string& account,
    bool shouldLike,
    std::optional<LikeStatus>& status,
    std::string& error) {
    if (!likeStatus(videoId, account, status, error)) {
        return false;
    }
    if (!status) {
        return true;
    }

    std::string escapedVideoId;
    std::string escapedAccount;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(account, escapedAccount, error)) {
        return false;
    }

    const std::string changeSql = shouldLike
        ? "INSERT IGNORE INTO video_likes (video_id, account) VALUES ('" +
              escapedVideoId + "', '" + escapedAccount + "')"
        : "DELETE FROM video_likes WHERE video_id = '" + escapedVideoId +
              "' AND account = '" + escapedAccount + "'";
    const std::string followupSql = shouldLike
        ? "UPDATE videos SET like_count = like_count + 1 WHERE video_id = '" +
              escapedVideoId + "'"
        : "UPDATE videos SET like_count = GREATEST(like_count - 1, 0) "
          "WHERE video_id = '" + escapedVideoId + "'";
    bool changed = false;
    if (!database_.executeIfChanged(
            changeSql, followupSql, changed, error)) {
        return false;
    }
    return likeStatus(videoId, account, status, error);
}

bool MySqlVideoRepository::watchProgress(
    const std::string& videoId,
    const std::string& account,
    std::optional<WatchProgress>& progress,
    std::string& error) {
    progress.reset();
    std::string escapedVideoId;
    std::string escapedAccount;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT COALESCE(wp.seconds, 0) FROM videos v "
        "LEFT JOIN video_watch_progress wp "
        "ON wp.video_id = v.video_id AND wp.account = '" + escapedAccount +
        "' WHERE v.video_id = '" + escapedVideoId +
        "' AND v.status = 1 LIMIT 1";
    if (!database_.query(sql, rows, error)) {
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
    } catch (const std::exception&) {
        error = "播放进度不是有效数字";
        return false;
    }
    return true;
}

bool MySqlVideoRepository::saveWatchProgress(
    const std::string& videoId,
    const std::string& account,
    int seconds,
    std::optional<WatchProgress>& progress,
    std::string& error) {
    if (!watchProgress(videoId, account, progress, error)) {
        return false;
    }
    if (!progress) {
        return true;
    }

    std::string escapedVideoId;
    std::string escapedAccount;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(account, escapedAccount, error)) {
        return false;
    }

    const std::string secondsValue = std::to_string(seconds);
    const std::string sql =
        "INSERT INTO video_watch_progress (video_id, account, seconds) "
        "VALUES ('" + escapedVideoId + "', '" + escapedAccount + "', " +
        secondsValue + ") ON DUPLICATE KEY UPDATE seconds = VALUES(seconds)";
    if (!database_.execute(sql, error)) {
        return false;
    }
    return watchProgress(videoId, account, progress, error);
}

bool MySqlVideoRepository::favoriteStatus(
    const std::string& videoId,
    const std::string& account,
    std::optional<FavoriteStatus>& status,
    std::string& error) {
    status.reset();
    std::string escapedVideoId;
    std::string escapedAccount;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT EXISTS(SELECT 1 FROM video_favorites vf "
        "WHERE vf.video_id = v.video_id AND vf.account = '" +
        escapedAccount + "') FROM videos v WHERE v.video_id = '" +
        escapedVideoId + "' AND v.status = 1 LIMIT 1";
    if (!database_.query(sql, rows, error)) {
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

bool MySqlVideoRepository::setFavorited(
    const std::string& videoId,
    const std::string& account,
    bool shouldFavorite,
    std::optional<FavoriteStatus>& status,
    std::string& error) {
    if (!favoriteStatus(videoId, account, status, error)) {
        return false;
    }
    if (!status) {
        return true;
    }

    std::string escapedVideoId;
    std::string escapedAccount;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(account, escapedAccount, error)) {
        return false;
    }

    const std::string sql = shouldFavorite
        ? "INSERT IGNORE INTO video_favorites (video_id, account) VALUES ('" +
              escapedVideoId + "', '" + escapedAccount + "')"
        : "DELETE FROM video_favorites WHERE video_id = '" + escapedVideoId +
              "' AND account = '" + escapedAccount + "'";
    if (!database_.execute(sql, error)) {
        return false;
    }
    return favoriteStatus(videoId, account, status, error);
}

bool MySqlVideoRepository::favoriteVideos(const std::string& account,
                                          std::vector<Video>& videos,
                                          std::string& error) {
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
        "WHERE videos.status = 1 AND vf.account = '" + escapedAccount +
        "' ORDER BY vf.created_at DESC, vf.id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto& row : rows) {
        Video video;
        if (!videoFromRow(row, video, error)) {
            videos.clear();
            return false;
        }
        videos.push_back(std::move(video));
    }
    return true;
}

bool MySqlVideoRepository::ownerVideos(const std::string& account,
                                       std::vector<Video>& videos,
                                       std::string& error) {
    videos.clear();
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql = VIDEO_SELECT +
        "WHERE status = 1 AND owner_account = '" + escapedAccount +
        "' ORDER BY published_on DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto& row : rows) {
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
    const std::string& videoId,
    std::optional<std::vector<VideoComment>>& comments,
    std::string& error) {
    comments.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }

    bool exists = false;
    if (!videoExists(database_, escapedVideoId, exists, error)) {
        return false;
    }
    if (!exists) {
        return true;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT LPAD(id, 3, '0'), video_id, user_name, account, content, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') FROM video_comments "
        "WHERE video_id = '" + escapedVideoId +
        "' ORDER BY created_at DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    std::vector<VideoComment> result;
    for (const auto& row : rows) {
        VideoComment comment;
        if (!commentFromRow(row, comment, error)) {
            return false;
        }
        result.push_back(std::move(comment));
    }
    comments = std::move(result);
    return true;
}

bool MySqlVideoRepository::addComment(
    const std::string& videoId,
    const std::string& userName,
    const std::string& account,
    const std::string& content,
    std::optional<VideoComment>& comment,
    std::string& error) {
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
    if (!videoExists(database_, escapedVideoId, exists, error)) {
        return false;
    }
    if (!exists) {
        return true;
    }

    const std::string insertSql =
        "INSERT INTO video_comments (video_id, user_name, account, content) "
        "VALUES ('" + escapedVideoId + "', '" + escapedUserName + "', '" +
        escapedAccount + "', '" + escapedContent + "')";
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
    const std::string& videoId,
    std::optional<std::vector<VideoBarrage>>& barrages,
    std::string& error) {
    barrages.reset();
    std::string escapedVideoId;
    if (!database_.escape(videoId, escapedVideoId, error)) {
        return false;
    }

    bool exists = false;
    if (!videoExists(database_, escapedVideoId, exists, error)) {
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
    for (const auto& row : rows) {
        VideoBarrage barrage;
        if (!barrageFromRow(row, barrage, error)) {
            return false;
        }
        result.push_back(std::move(barrage));
    }
    barrages = std::move(result);
    return true;
}

bool MySqlVideoRepository::addBarrage(
    const std::string& videoId,
    int seconds,
    const std::string& text,
    std::optional<VideoBarrage>& barrage,
    std::string& error) {
    barrage.reset();
    std::string escapedVideoId;
    std::string escapedText;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(text, escapedText, error)) {
        return false;
    }

    bool exists = false;
    if (!videoExists(database_, escapedVideoId, exists, error)) {
        return false;
    }
    if (!exists) {
        return true;
    }

    const std::string insertSql =
        "INSERT INTO video_barrages (video_id, seconds, text) VALUES ('" +
        escapedVideoId + "', " + std::to_string(seconds) + ", '" +
        escapedText + "')";
    if (!database_.execute(insertSql, error)) {
        return false;
    }
    barrage = VideoBarrage{seconds, text};
    return true;
}

bool MySqlVideoRepository::userProfile(const std::string& account,
                                       std::optional<UserProfile>& profile,
                                       std::string& error) {
    profile.reset();
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT account, user_name, description, avatar_path FROM users "
        "WHERE account = '" + escapedAccount + "' LIMIT 1";
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

bool MySqlVideoRepository::updateUserProfile(
    const std::string& account,
    const std::string& userName,
    const std::string& description,
    std::optional<UserProfile>& profile,
    std::string& error) {
    if (!userProfile(account, profile, error)) {
        return false;
    }
    if (!profile) {
        return true;
    }

    std::string escapedAccount;
    std::string escapedUserName;
    std::string escapedDescription;
    if (!database_.escape(account, escapedAccount, error) ||
        !database_.escape(userName, escapedUserName, error) ||
        !database_.escape(description, escapedDescription, error)) {
        return false;
    }

    const std::string sql =
        "UPDATE users SET user_name = '" + escapedUserName +
        "', description = '" + escapedDescription +
        "' WHERE account = '" + escapedAccount + "'";
    if (!database_.execute(sql, error)) {
        return false;
    }
    return userProfile(account, profile, error);
}

bool MySqlVideoRepository::passwordLogin(
    const std::string& account,
    const std::string& password,
    std::optional<UserProfile>& profile,
    std::string& error) {
    profile.reset();
    std::string escapedAccount;
    std::string escapedPassword;
    if (!database_.escape(account, escapedAccount, error) ||
        !database_.escape(password, escapedPassword, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT account, user_name, description, avatar_path FROM users "
        "WHERE account = '" + escapedAccount + "' AND password = '" +
        escapedPassword + "' LIMIT 1";
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

bool MySqlVideoRepository::createEmailCode(const std::string& email,
                                           EmailCodeSession& session,
                                           std::string& error) {
    const std::string normalizedEmail = lowerCopy(email);
    static const std::regex EMAIL_RE(
        R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    if (!std::regex_match(normalizedEmail, EMAIL_RE)) {
        error = "邮箱格式错误";
        return true;
    }

    const std::string debugCode = "246810";
    std::string escapedEmail;
    std::string escapedCode;
    if (!database_.escape(normalizedEmail, escapedEmail, error) ||
        !database_.escape(debugCode, escapedCode, error)) {
        return false;
    }

    const std::string insertSql =
        "INSERT INTO email_login_codes (authcode_id, email, authcode) "
        "VALUES ('pending', '" + escapedEmail + "', '" + escapedCode + "')";
    if (!database_.execute(insertSql, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string updateSql =
        "UPDATE email_login_codes SET authcode_id = "
        "CONCAT('email-code-', LPAD(id, 3, '0')) "
        "WHERE id = LAST_INSERT_ID()";
    if (!database_.execute(updateSql, error)) {
        return false;
    }
    const std::string selectSql =
        "SELECT authcode_id, authcode FROM email_login_codes "
        "WHERE id = LAST_INSERT_ID()";
    if (!database_.query(selectSql, rows, error)) {
        return false;
    }
    if (rows.empty() || rows.front().size() != 2) {
        error = "验证码会话创建后无法读取";
        return false;
    }

    session.authcodeId = valueOrEmpty(rows.front()[0]);
    session.debugCode = valueOrEmpty(rows.front()[1]);
    return true;
}

bool MySqlVideoRepository::emailLogin(
    const std::string& email,
    const std::string& authcodeId,
    const std::string& authcode,
    std::optional<UserProfile>& profile,
    std::string& error) {
    profile.reset();
    const std::string normalizedEmail = lowerCopy(email);
    std::string escapedEmail;
    std::string escapedAuthcodeId;
    std::string escapedAuthcode;
    if (!database_.escape(normalizedEmail, escapedEmail, error) ||
        !database_.escape(authcodeId, escapedAuthcodeId, error) ||
        !database_.escape(authcode, escapedAuthcode, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string codeSql =
        "SELECT id FROM email_login_codes WHERE authcode_id = '" +
        escapedAuthcodeId + "' AND email = '" + escapedEmail +
        "' AND authcode = '" + escapedAuthcode +
        "' AND consumed = 0 LIMIT 1";
    if (!database_.query(codeSql, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        return true;
    }

    const std::string consumeSql =
        "UPDATE email_login_codes SET consumed = 1 WHERE id = " +
        valueOrEmpty(rows.front()[0]);
    if (!database_.execute(consumeSql, error)) {
        return false;
    }

    std::optional<UserProfile> existing;
    if (!userProfile(normalizedEmail, existing, error)) {
        return false;
    }
    if (!existing) {
        std::string escapedUserName;
        const std::string userName = nameFromEmail(normalizedEmail);
        if (!database_.escape(userName, escapedUserName, error)) {
            return false;
        }
        const std::string insertUserSql =
            "INSERT INTO users (account, password, user_name, description, "
            "avatar_path) VALUES ('" + escapedEmail + "', '', '" +
            escapedUserName + "', '', '')";
        if (!database_.execute(insertUserSql, error)) {
            return false;
        }
    }
    return userProfile(normalizedEmail, profile, error);
}

bool MySqlVideoRepository::logout(const std::string& account,
                                  bool& knownUser,
                                  std::string& error) {
    std::optional<UserProfile> profile;
    if (!userProfile(account, profile, error)) {
        return false;
    }
    knownUser = profile.has_value();
    return true;
}

bool MySqlVideoRepository::adminReviews(std::vector<AdminReview>& reviews,
                                        std::string& error) {
    reviews.clear();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT video_id, title, owner_account, review_status, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') "
        "FROM videos ORDER BY created_at DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto& row : rows) {
        AdminReview review;
        if (!reviewFromRow(row, review, error)) {
            reviews.clear();
            return false;
        }
        reviews.push_back(std::move(review));
    }
    return true;
}

bool MySqlVideoRepository::updateReviewStatus(const std::string& videoId,
                                              const std::string& status,
                                              bool& updated,
                                              std::string& error) {
    updated = false;
    if (status != "审核通过" && status != "审核拒绝") {
        error = "审核参数错误";
        return true;
    }

    std::string escapedVideoId;
    std::string escapedStatus;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(status, escapedStatus, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string existsSql =
        "SELECT 1 FROM videos WHERE video_id = '" + escapedVideoId +
        "' LIMIT 1";
    if (!database_.query(existsSql, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        error = "审核参数错误";
        return true;
    }

    const std::string updateSql =
        "UPDATE videos SET review_status = '" + escapedStatus +
        "' WHERE video_id = '" + escapedVideoId + "'";
    if (!database_.execute(updateSql, error)) {
        return false;
    }
    updated = true;
    return true;
}

bool MySqlVideoRepository::adminUsers(std::vector<AdminUser>& users,
                                      std::string& error) {
    users.clear();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT account, user_name, role, status, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') "
        "FROM users ORDER BY created_at ASC, id ASC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto& row : rows) {
        AdminUser user;
        if (!adminUserFromRow(row, user, error)) {
            users.clear();
            return false;
        }
        users.push_back(std::move(user));
    }
    return true;
}

bool MySqlVideoRepository::updateAdminUser(const std::string& account,
                                           const std::string& action,
                                           bool& updated,
                                           std::string& error) {
    updated = false;
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) {
        return false;
    }

    std::vector<bitedb::Database::QueryRow> rows;
    const std::string existsSql =
        "SELECT 1 FROM users WHERE account = '" + escapedAccount +
        "' LIMIT 1";
    if (!database_.query(existsSql, rows, error)) {
        return false;
    }
    if (rows.empty()) {
        error = "用户不存在";
        return true;
    }

    std::string sql;
    if (action == "set-admin") {
        sql = "UPDATE users SET role = '管理员' WHERE account = '" +
            escapedAccount + "'";
    } else if (action == "disable") {
        sql = "UPDATE users SET status = '禁用' WHERE account = '" +
            escapedAccount + "'";
    } else if (action == "enable") {
        sql = "UPDATE users SET status = '启用' WHERE account = '" +
            escapedAccount + "'";
    } else if (action == "delete") {
        sql = "DELETE FROM users WHERE account = '" + escapedAccount + "'";
    } else {
        error = "角色操作不支持";
        return true;
    }

    if (!database_.execute(sql, error)) {
        return false;
    }
    updated = true;
    return true;
}

}  // namespace bitevideo
