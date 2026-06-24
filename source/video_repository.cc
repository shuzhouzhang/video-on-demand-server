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

}  // namespace bitevideo
