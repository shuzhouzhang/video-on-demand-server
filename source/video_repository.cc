#include "video_repository.h"

#include "util.h"

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

}  // namespace bitevideo
