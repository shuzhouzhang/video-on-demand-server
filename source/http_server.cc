#include "http_server.h"

#include "util.h"

namespace biteserver {

HttpServer::HttpServer() {
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
