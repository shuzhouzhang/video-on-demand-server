#include "http_server.h"

#include "bitelog.h"
#include "util.h"

namespace biteserver {
namespace {

void setJsonResponse(httplib::Response& response,
                     int status,
                     const Json::Value& body) {
    response.status = status;
    response.set_content(
        biteutil::JSON::serialize(body).value_or(
            R"({"success":false,"message":"serialization error"})"),
        "application/json; charset=utf-8");
}

std::string accountOrGuest(const httplib::Request& request) {
    return request.has_param("account") &&
        !request.get_param_value("account").empty()
        ? request.get_param_value("account") : "guest";
}

std::string accountOrGuest(const Json::Value& payload) {
    const std::string account = payload["account"].asString();
    return account.empty() ? "guest" : account;
}

std::size_t utf8CharCount(const std::string& value) {
    std::size_t count = 0;
    for (unsigned char ch : value) {
        if ((ch & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

std::string utf8Prefix(const std::string& value, std::size_t maxChars) {
    std::size_t chars = 0;
    std::size_t bytes = 0;
    while (bytes < value.size() && chars < maxChars) {
        const unsigned char ch = static_cast<unsigned char>(value[bytes]);
        std::size_t step = 1;
        if ((ch & 0x80) == 0) {
            step = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            step = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            step = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            step = 4;
        }
        if (bytes + step > value.size()) {
            break;
        }
        bytes += step;
        ++chars;
    }
    return value.substr(0, bytes);
}

Json::Value commentToJson(const bitevideo::VideoComment& comment) {
    Json::Value value;
    value["id"] = comment.id;
    value["videoId"] = comment.videoId;
    value["userName"] = comment.userName;
    value["account"] = comment.account;
    value["content"] = comment.content;
    value["createdAt"] = comment.createdAt;
    return value;
}

Json::Value barrageToJson(const bitevideo::VideoBarrage& barrage) {
    Json::Value value;
    value["seconds"] = barrage.seconds;
    value["text"] = barrage.text;
    return value;
}

Json::Value userProfileToJson(const bitevideo::UserProfile& profile) {
    Json::Value value;
    value["account"] = profile.account;
    value["userName"] = profile.userName;
    value["description"] = profile.description;
    value["avatarPath"] = profile.avatarPath;
    return value;
}

}  // namespace

HttpServer::HttpServer(bitevideo::VideoStore& videoStore)
    : videoStore_(videoStore) {
    registerRoutes();
}

void HttpServer::registerRoutes() {
    server_.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        Json::Value body;
        body["code"] = 0;
        body["message"] = "ok";
        body["data"]["status"] = "UP";

        const auto json = biteutil::JSON::serialize(body);
        if (!json) {
            response.status = 500;
            response.set_content(
                R"({"code":500,"message":"response serialization failed"})",
                "application/json");
            return;
        }

        response.status = 200;
        response.set_content(*json, "application/json");
    });

    server_.Get("/videos", [this](const httplib::Request&,
                                  httplib::Response& response) {
        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!videoStore_.list(videos, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos failed: {}", error);
            }
            Json::Value body;
            body["success"] = false;
            body["message"] = "视频列表暂时不可用";
            response.status = 500;
            response.set_content(
                biteutil::JSON::serialize(body).value_or(
                    R"({"success":false,"message":"database error"})"),
                "application/json; charset=utf-8");
            return;
        }

        Json::Value body(Json::arrayValue);
        for (const bitevideo::Video& video : videos) {
            body.append(bitevideo::toJson(video));
        }
        response.status = 200;
        response.set_content(
            biteutil::JSON::serialize(body).value_or("[]"),
            "application/json; charset=utf-8");
    });

    server_.Get("/videos/detail", [this](const httplib::Request& request,
                                         httplib::Response& response) {
        Json::Value body;
        const std::string videoId =
            request.has_param("id") ? request.get_param_value("id") : "";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
        } else {
            std::optional<bitevideo::Video> video;
            std::string error;
            if (!videoStore_.findById(videoId, video, error)) {
                if (bitelog::g_logger) {
                    ERR("GET /videos/detail failed: {}", error);
                }
                response.status = 500;
                body["success"] = false;
                body["message"] = "视频详情暂时不可用";
            } else if (!video) {
                body["success"] = false;
                body["message"] = "视频不存在";
            } else {
                body["success"] = true;
                body["video"] = bitevideo::toJson(*video);
            }
        }

        if (response.status == -1) {
            response.status = 200;
        }
        response.set_content(
            biteutil::JSON::serialize(body).value_or(
                R"({"success":false,"message":"serialization error"})"),
            "application/json; charset=utf-8");
    });

    server_.Get("/videos/search", [this](const httplib::Request& request,
                                         httplib::Response& response) {
        Json::Value body;
        const std::string keyword = request.has_param("keyword")
            ? request.get_param_value("keyword") : "";
        if (keyword.empty()) {
            body["success"] = false;
            body["message"] = "搜索关键词不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!videoStore_.search(keyword, videos, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/search failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "视频搜索暂时不可用";
            setJsonResponse(response, 500, body);
            return;
        }

        body["success"] = true;
        body["videos"] = Json::arrayValue;
        for (const bitevideo::Video& video : videos) {
            body["videos"].append(bitevideo::toJson(video));
        }
        setJsonResponse(response, 200, body);
    });

    server_.Get("/videos/play-url", [this](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<std::string> playUrl;
        std::string error;
        if (!videoStore_.playUrl(videoId, playUrl, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/play-url failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "播放地址暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!playUrl) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["videoId"] = videoId;
            body["playUrl"] = *playUrl;
            setJsonResponse(response, 200, body);
        }
    });

    server_.Get("/videos/like-status", [this](const httplib::Request& request,
                                              httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string account = request.has_param("account") &&
            !request.get_param_value("account").empty()
            ? request.get_param_value("account") : "guest";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::LikeStatus> status;
        std::string error;
        if (!videoStore_.likeStatus(videoId, account, status, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/like-status failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "点赞状态暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!status) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["liked"] = status->liked;
            body["likeCount"] = status->likeCount;
            setJsonResponse(response, 200, body);
        }
    });

    const auto changeLike = [this](const httplib::Request& request,
                                   httplib::Response& response,
                                   bool shouldLike) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        std::string account = (*payload)["account"].asString();
        if (account.empty()) {
            account = "guest";
        }
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::LikeStatus> status;
        std::string error;
        if (!videoStore_.setLiked(videoId, account, shouldLike, status, error)) {
            if (bitelog::g_logger) {
                ERR("video like change failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "点赞操作暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!status) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["liked"] = status->liked;
            body["likeCount"] = status->likeCount;
            setJsonResponse(response, 200, body);
        }
    };

    server_.Post("/videos/like",
                 [changeLike](const httplib::Request& request,
                              httplib::Response& response) {
                     changeLike(request, response, true);
                 });
    server_.Post("/videos/unlike",
                 [changeLike](const httplib::Request& request,
                              httplib::Response& response) {
                     changeLike(request, response, false);
                 });

    server_.Get("/videos/watch-progress",
                [this](const httplib::Request& request,
                       httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string account = accountOrGuest(request);
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::WatchProgress> progress;
        std::string error;
        if (!videoStore_.watchProgress(videoId, account, progress, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/watch-progress failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "播放进度暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!progress) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["seconds"] = progress->seconds;
            body["message"] = "读取成功";
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/videos/watch-progress",
                 [this](const httplib::Request& request,
                        httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        const std::string account = accountOrGuest(*payload);
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!(*payload)["seconds"].isInt() || (*payload)["seconds"].asInt() < 0) {
            body["success"] = false;
            body["message"] = "播放秒数非法";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::WatchProgress> progress;
        std::string error;
        if (!videoStore_.saveWatchProgress(
                videoId, account, (*payload)["seconds"].asInt(), progress, error)) {
            if (bitelog::g_logger) {
                ERR("POST /videos/watch-progress failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "播放进度保存失败";
            setJsonResponse(response, 500, body);
        } else if (!progress) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["seconds"] = progress->seconds;
            body["message"] = "保存成功";
            setJsonResponse(response, 200, body);
        }
    });

    server_.Get("/videos/favorite-status",
                [this](const httplib::Request& request,
                       httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string account = request.has_param("account")
            ? request.get_param_value("account") : "";
        if (videoId.empty() || account.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 和账号不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::FavoriteStatus> status;
        std::string error;
        if (!videoStore_.favoriteStatus(videoId, account, status, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/favorite-status failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "收藏状态暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!status) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["favorited"] = status->favorited;
            setJsonResponse(response, 200, body);
        }
    });

    const auto changeFavorite = [this](const httplib::Request& request,
                                       httplib::Response& response,
                                       bool shouldFavorite) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        const std::string account = (*payload)["account"].asString();
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后再收藏视频";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::FavoriteStatus> status;
        std::string error;
        if (!videoStore_.setFavorited(
                videoId, account, shouldFavorite, status, error)) {
            if (bitelog::g_logger) {
                ERR("video favorite change failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "收藏操作暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!status) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["favorited"] = status->favorited;
            setJsonResponse(response, 200, body);
        }
    };

    server_.Post("/videos/favorite",
                 [changeFavorite](const httplib::Request& request,
                                  httplib::Response& response) {
                     changeFavorite(request, response, true);
                 });
    server_.Post("/videos/unfavorite",
                 [changeFavorite](const httplib::Request& request,
                                  httplib::Response& response) {
                     changeFavorite(request, response, false);
                 });

    server_.Get("/users/favorites", [this](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        const std::string account = request.has_param("account")
            ? request.get_param_value("account") : "";
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后查看收藏";
            setJsonResponse(response, 200, body);
            return;
        }

        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!videoStore_.favoriteVideos(account, videos, error)) {
            if (bitelog::g_logger) {
                ERR("GET /users/favorites failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "我的收藏暂时不可用";
            setJsonResponse(response, 500, body);
            return;
        }

        body["success"] = true;
        body["videos"] = Json::arrayValue;
        for (const bitevideo::Video& video : videos) {
            body["videos"].append(bitevideo::toJson(video));
        }
        setJsonResponse(response, 200, body);
    });

    server_.Get("/videos/comments", [this](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<std::vector<bitevideo::VideoComment>> comments;
        std::string error;
        if (!videoStore_.comments(videoId, comments, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/comments failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "评论列表暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!comments) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["comments"] = Json::arrayValue;
            for (const auto& comment : *comments) {
                body["comments"].append(commentToJson(comment));
            }
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/videos/comments", [this](const httplib::Request& request,
                                            httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        const std::string userName = (*payload)["userName"].asString();
        const std::string account = (*payload)["account"].asString();
        const std::string content = (*payload)["content"].asString();
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (account.empty() || userName.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后再发表评论";
            setJsonResponse(response, 200, body);
            return;
        }
        if (content.empty() || utf8CharCount(content) > 200) {
            body["success"] = false;
            body["message"] = "评论内容需为 1 到 200 个字符";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::VideoComment> comment;
        std::string error;
        if (!videoStore_.addComment(
                videoId, userName, account, content, comment, error)) {
            if (bitelog::g_logger) {
                ERR("POST /videos/comments failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "评论发送失败";
            setJsonResponse(response, 500, body);
        } else if (!comment) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "评论成功";
            body["comment"] = commentToJson(*comment);
            setJsonResponse(response, 200, body);
        }
    });

    server_.Get("/videos/barrages", [this](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频标识不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<std::vector<bitevideo::VideoBarrage>> barrages;
        std::string error;
        if (!videoStore_.barrages(videoId, barrages, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/barrages failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "弹幕列表暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!barrages) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["barrages"] = Json::arrayValue;
            for (const auto& barrage : *barrages) {
                body["barrages"].append(barrageToJson(barrage));
            }
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/videos/barrages", [this](const httplib::Request& request,
                                            httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        const std::string text = (*payload)["text"].asString();
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频标识不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!(*payload)["seconds"].isInt() || (*payload)["seconds"].asInt() < 0) {
            body["success"] = false;
            body["message"] = "弹幕时间非法";
            setJsonResponse(response, 200, body);
            return;
        }
        if (text.empty()) {
            body["success"] = false;
            body["message"] = "弹幕内容不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string clippedText = utf8CharCount(text) > 30
            ? utf8Prefix(text, 30) : text;
        std::optional<bitevideo::VideoBarrage> barrage;
        std::string error;
        if (!videoStore_.addBarrage(
                videoId, (*payload)["seconds"].asInt(), clippedText,
                barrage, error)) {
            if (bitelog::g_logger) {
                ERR("POST /videos/barrages failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "弹幕发送失败";
            setJsonResponse(response, 500, body);
        } else if (!barrage) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "发送成功";
            body["seconds"] = barrage->seconds;
            body["text"] = barrage->text;
            setJsonResponse(response, 200, body);
        }
    });

    server_.Get("/users/profile", [this](const httplib::Request& request,
                                         httplib::Response& response) {
        Json::Value body;
        const std::string account = request.has_param("account")
            ? request.get_param_value("account") : "";
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::UserProfile> profile;
        std::string error;
        if (!videoStore_.userProfile(account, profile, error)) {
            if (bitelog::g_logger) {
                ERR("GET /users/profile failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "个人资料暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!profile) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["user"] = userProfileToJson(*profile);
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/users/profile", [this](const httplib::Request& request,
                                          httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string account = (*payload)["account"].asString();
        const std::string userName = (*payload)["userName"].asString();
        const std::string description = (*payload)["description"].asString();
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
            return;
        }
        if (userName.empty() || utf8CharCount(userName) > 20) {
            body["success"] = false;
            body["message"] = "昵称需为 1 到 20 个字符";
            setJsonResponse(response, 200, body);
            return;
        }
        if (utf8CharCount(description) > 100) {
            body["success"] = false;
            body["message"] = "个人简介不能超过 100 个字符";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::UserProfile> profile;
        std::string error;
        if (!videoStore_.updateUserProfile(
                account, userName, description, profile, error)) {
            if (bitelog::g_logger) {
                ERR("POST /users/profile failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "个人资料保存失败";
            setJsonResponse(response, 500, body);
        } else if (!profile) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "保存成功";
            body["user"] = userProfileToJson(*profile);
            setJsonResponse(response, 200, body);
        }
    });
}

bool HttpServer::listen(const std::string& host, std::uint16_t port) {
    return server_.listen(host, port);
}

int HttpServer::bindToAnyPort(const std::string& host) {
    return server_.bind_to_any_port(host);
}

bool HttpServer::listenAfterBind() {
    return server_.listen_after_bind();
}

void HttpServer::stop() {
    server_.stop();
}

}  // namespace biteserver
