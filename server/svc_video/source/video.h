/*
 * 视频列表模型：字段名与Qt客户端VideoInfo保持一一对应。
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <jsoncpp/json/json.h>

namespace bitevideo {

struct Video {
    std::string id;
    std::string title;
    std::string userName;
    std::string date;
    std::size_t durationSeconds;
    std::string playCount;
    std::string likeCount;
    std::string category;
    std::vector<std::string> tags;
    std::string description;
};

std::string formatDuration(std::size_t seconds);
Json::Value toJson(const Video& video);

}  // namespace bitevideo
