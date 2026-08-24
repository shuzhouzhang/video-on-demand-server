#pragma once

#include <string>

namespace bitevideo {

struct LikeStatus {
    bool liked = false;
    std::string likeCount;
};

struct WatchProgress {
    int seconds = 0;
};

struct FavoriteStatus {
    bool favorited = false;
};

struct VideoComment {
    std::string id;
    std::string videoId;
    std::string userName;
    std::string account;
    std::string content;
    std::string createdAt;
};

struct VideoBarrage {
    int seconds = 0;
    std::string text;
};

}  // namespace bitevideo
