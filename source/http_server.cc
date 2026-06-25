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
