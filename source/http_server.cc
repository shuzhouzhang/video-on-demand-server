#include "http_server.h"

#include "bitelog.h"
#include "util.h"

namespace biteserver {

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
