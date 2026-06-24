#include "video_repository.h"

#include "util.h"

namespace bitevideo {
namespace {

constexpr std::size_t VIDEO_FIELD_COUNT = 10;

std::string valueOrEmpty(const std::optional<std::string>& value) {
    return value.value_or("");
}

}  // namespace

MySqlVideoRepository::MySqlVideoRepository(bitedb::Database& database)
    : database_(database) {}

bool MySqlVideoRepository::list(std::vector<Video>& videos,
                                std::string& error) {
    videos.clear();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT video_id, title, user_name, "
        "DATE_FORMAT(published_on, '%c-%e'), duration_seconds, "
        "CAST(play_count AS CHAR), CAST(like_count AS CHAR), category, "
        "CAST(tags AS CHAR), description "
        "FROM videos WHERE status = 1 ORDER BY published_on DESC, id DESC";
    if (!database_.query(sql, rows, error)) {
        return false;
    }

    for (const auto& row : rows) {
        if (row.size() != VIDEO_FIELD_COUNT) {
            error = "视频查询返回了不符合预期的字段数量";
            videos.clear();
            return false;
        }

        Video video;
        video.id = valueOrEmpty(row[0]);
        video.title = valueOrEmpty(row[1]);
        video.userName = valueOrEmpty(row[2]);
        video.date = valueOrEmpty(row[3]);
        try {
            video.durationSeconds =
                static_cast<std::size_t>(std::stoull(valueOrEmpty(row[4])));
        } catch (const std::exception&) {
            error = "视频时长不是有效数字: " + video.id;
            videos.clear();
            return false;
        }
        video.playCount = valueOrEmpty(row[5]);
        video.likeCount = valueOrEmpty(row[6]);
        video.category = valueOrEmpty(row[7]);
        video.description = valueOrEmpty(row[9]);

        const auto tags = biteutil::JSON::unserialize(valueOrEmpty(row[8]));
        if (!tags || !tags->isArray()) {
            error = "视频标签不是有效JSON数组: " + video.id;
            videos.clear();
            return false;
        }
        for (const Json::Value& tag : *tags) {
            if (tag.isString()) {
                video.tags.push_back(tag.asString());
            }
        }
        videos.push_back(std::move(video));
    }
    return true;
}

}  // namespace bitevideo
