#include "http_server.h"
#include "route_support.h"
#include "util.h"
#include <utility>
namespace biteserver {
using detail::setJsonResponse;
// HTTP 外壳只负责监听、请求体上限和健康检查；业务路由由服务显式注入。
HttpServer::HttpServer(std::string serviceName, const RouteRegistrar& registrar)
    : serviceName_(std::move(serviceName)) {
    server_.set_payload_max_length(80 * 1024 * 1024);
    registerHealthRoutes();
    registrar(server_);
}
void HttpServer::registerHealthRoutes() {
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

    server_.Get("/healthz", [this](const httplib::Request&,
                                   httplib::Response& response) {
        Json::Value body;
        body["success"] = true;
        body["service"] = serviceName_;
        body["status"] = "ok";
        setJsonResponse(response, 200, body);
    });
}

bool HttpServer::listen(const std::string& host, std::uint16_t port) {
    return server_.listen(host.c_str(), port);
}

int HttpServer::bindToAnyPort(const std::string& host) {
    return server_.bind_to_any_port(host.c_str());
}

bool HttpServer::listenAfterBind() {
    return server_.listen_after_bind();
}

void HttpServer::stop() {
    server_.stop();
}

}  // namespace biteserver
