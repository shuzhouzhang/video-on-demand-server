#pragma once

#include <string>
#include <vector>

namespace bitevideo {

struct VideoDraft {
    std::string title;
    std::string userName;
    std::string account;
    std::string category;
    std::vector<std::string> tags;
    std::string description;
    std::string playUrl;
    std::string coverPath;
    std::string videoFileName;
    std::string coverFileName;
};

} // namespace bitevideo
