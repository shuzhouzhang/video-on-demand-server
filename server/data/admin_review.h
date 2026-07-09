#pragma once

#include <string>

namespace bitevideo {

struct AdminReview {
    std::string videoId;
    std::string title;
    std::string userId;
    std::string status;
    std::string uploadTime;
};

}  // namespace bitevideo
