#include "video.h"

#include <iomanip>
#include <sstream>

namespace bitevideo {

std::string formatDuration(std::size_t seconds) {
    std::ostringstream output;
    output << std::setw(2) << std::setfill('0') << seconds / 60 << ':'
           << std::setw(2) << std::setfill('0') << seconds % 60;
    return output.str();
}

Json::Value toJson(const Video& video) {
    Json::Value value;
    value["id"] = video.id;
    value["title"] = video.title;
    value["userName"] = video.userName;
    value["date"] = video.date;
    value["duration"] = formatDuration(video.durationSeconds);
    value["playCount"] = video.playCount;
    value["likeCount"] = video.likeCount;
    value["category"] = video.category;
    value["description"] = video.description;
    for (const std::string& tag : video.tags) {
        value["tags"].append(tag);
    }
    return value;
}

}  // namespace bitevideo
