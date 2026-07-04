#include "util.h"

#include <httplib.h>
#include <jsoncpp/json/json.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

struct Downstream {
    std::string name;
    std::string baseUrl;
};

struct GatewaySettings {
    std::uint16_t port = 9000;
    Downstream user{"user_service", "http://127.0.0.1:9101"};
    Downstream video{"video_service", "http://127.0.0.1:9102"};
    Downstream interaction{"interaction_service", "http://127.0.0.1:9103"};
    int timeoutMs = 3000;
};

struct ParsedUrl {
    std::string scheme;
    std::string host;
    int port = 80;
};

std::atomic<unsigned long long> g_requestCounter{0};

void setJsonResponse(httplib::Response& response,
                     int status,
                     const Json::Value& body) {
    response.status = status;
    response.set_content(
        biteutil::JSON::serialize(body).value_or(
            R"({"success":false,"message":"serialization error"})"),
        "application/json; charset=utf-8");
}

std::optional<Json::Value> readJsonFile(const std::string& path,
                                        std::string& error) {
    std::string body;
    if (!biteutil::FUTIL::read(path, body)) {
        error = "无法读取配置文件: " + path;
        return std::nullopt;
    }
    const auto json = biteutil::JSON::unserialize(body);
    if (!json || !json->isObject()) {
        error = "配置文件不是有效 JSON 对象: " + path;
        return std::nullopt;
    }
    return json;
}

bool loadGatewayPort(const std::string& path, GatewaySettings& settings,
                     std::string& error) {
    const auto root = readJsonFile(path, error);
    if (!root) {
        return false;
    }
    const Json::Value& port = (*root)["server"]["port"];
    if (!port.isInt() || port.asInt() < 1 || port.asInt() > 65535) {
        error = "gateway server.port 必须是 1 到 65535 之间的整数";
        return false;
    }
    settings.port = static_cast<std::uint16_t>(port.asInt());
    return true;
}

bool loadServices(const std::string& path, GatewaySettings& settings,
                  std::string& error) {
    const auto root = readJsonFile(path, error);
    if (!root) {
        return false;
    }
    const auto loadUrl = [&](const char* key, Downstream& downstream) {
        if ((*root)[key].isString() && !(*root)[key].asString().empty()) {
            downstream.baseUrl = (*root)[key].asString();
        }
    };
    loadUrl("user_service", settings.user);
    loadUrl("video_service", settings.video);
    loadUrl("interaction_service", settings.interaction);
    if ((*root)["timeout_ms"].isInt() && (*root)["timeout_ms"].asInt() > 0) {
        settings.timeoutMs = (*root)["timeout_ms"].asInt();
    }
    return true;
}

std::optional<ParsedUrl> parseHttpUrl(const std::string& url) {
    constexpr char prefix[] = "http://";
    if (url.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    std::string hostPort = url.substr(sizeof(prefix) - 1);
    const std::size_t slash = hostPort.find('/');
    if (slash != std::string::npos) {
        hostPort = hostPort.substr(0, slash);
    }
    ParsedUrl parsed;
    parsed.scheme = "http";
    const std::size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos) {
        parsed.host = hostPort;
        parsed.port = 80;
    } else {
        parsed.host = hostPort.substr(0, colon);
        parsed.port = std::stoi(hostPort.substr(colon + 1));
    }
    if (parsed.host.empty() || parsed.port < 1 || parsed.port > 65535) {
        return std::nullopt;
    }
    return parsed;
}

std::string requestPathWithQuery(const httplib::Request& request) {
    if (!request.target.empty()) {
        return request.target;
    }
    std::ostringstream path;
    path << request.path;
    bool first = true;
    for (const auto& param : request.params) {
        path << (first ? '?' : '&') << param.first << '=' << param.second;
        first = false;
    }
    return path.str();
}

std::string makeRequestId() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "gw-" + std::to_string(now) + "-" +
        std::to_string(++g_requestCounter);
}

const Downstream* selectDownstream(const GatewaySettings& settings,
                                   const std::string& path) {
    if (path == "/login" || path == "/login/password" ||
        path == "/login/email-code" || path == "/login/email" ||
        path == "/logout" || path == "/users/profile" ||
        path == "/users/avatar" || path == "/admin/users" ||
        path == "/admin/users/action") {
        return &settings.user;
    }
    if (path == "/videos/like" || path == "/videos/unlike" ||
        path == "/videos/like-status" || path == "/videos/favorite" ||
        path == "/videos/unfavorite" || path == "/videos/favorite-status" ||
        path == "/users/favorites" || path == "/videos/watch-progress" ||
        path == "/videos/comments" || path == "/videos/barrages") {
        return &settings.interaction;
    }
    if (path == "/videos" || path == "/videos/detail" ||
        path == "/videos/search" || path == "/videos/play-url" ||
        path == "/videos/upload" || path == "/users/videos" ||
        path == "/admin/reviews" || path == "/admin/reviews/action" ||
        path == "/__smoke-cleanup" || path.rfind("/uploads/", 0) == 0) {
        return &settings.video;
    }
    return nullptr;
}

void logGatewayError(const std::string& requestId,
                     const std::string& route,
                     const Downstream& downstream,
                     const std::string& downstreamUrl,
                     const std::string& reason) {
    std::cerr << "request_id=" << requestId
              << " gateway_route=" << route
              << " downstream_service=" << downstream.name
              << " downstream_url=" << downstreamUrl
              << " error=" << reason << '\n';
}

void forwardToDownstream(const GatewaySettings& settings,
                         const Downstream& downstream,
                         const httplib::Request& request,
                         httplib::Response& response) {
    const std::string requestId = request.has_header("X-Request-Id")
        ? request.get_header_value("X-Request-Id") : makeRequestId();
    const std::string target = requestPathWithQuery(request);
    const std::string downstreamUrl = downstream.baseUrl + target;
    const auto parsed = parseHttpUrl(downstream.baseUrl);
    if (!parsed) {
        Json::Value body;
        body["success"] = false;
        body["message"] = "downstream service unavailable";
        logGatewayError(requestId, request.path, downstream, downstreamUrl,
                        "invalid downstream url");
        setJsonResponse(response, 502, body);
        return;
    }

    httplib::Client client(parsed->host, parsed->port);
    const std::chrono::milliseconds timeout(settings.timeoutMs);
    client.set_connection_timeout(timeout);
    client.set_read_timeout(timeout);
    client.set_write_timeout(timeout);

    httplib::Headers headers = request.headers;
    headers.erase("Host");
    headers.erase("Content-Length");
    headers.erase("Transfer-Encoding");
    headers.erase("X-Request-Id");
    headers.emplace("X-Request-Id", requestId);

    const std::string contentType = request.get_header_value("Content-Type");
    if (request.method != "GET" && request.method != "POST") {
        Json::Value body;
        body["success"] = false;
        body["message"] = "gateway method not allowed";
        setJsonResponse(response, 405, body);
        return;
    }
    httplib::Result result = request.method == "GET"
        ? client.Get(target.c_str(), headers)
        : client.Post(target.c_str(), headers, request.body, contentType.c_str());
    if (!result) {
        Json::Value body;
        body["success"] = false;
        body["message"] = "downstream service unavailable";
        logGatewayError(requestId, request.path, downstream, downstreamUrl,
                        httplib::to_string(result.error()));
        setJsonResponse(response, 502, body);
        return;
    }

    response.status = result->status;
    response.headers = result->headers;
    response.set_header("X-Request-Id", requestId);
    const std::string responseContentType = result->get_header_value("Content-Type");
    response.set_content(result->body, responseContentType.empty()
        ? "application/octet-stream" : responseContentType.c_str());
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string gatewayConfig = argc > 1 ? argv[1] : "conf/gateway.local.json";
    const std::string servicesConfig = argc > 2 ? argv[2] : "conf/services.local.json";

    GatewaySettings settings;
    std::string error;
    if (!loadGatewayPort(gatewayConfig, settings, error) ||
        !loadServices(servicesConfig, settings, error)) {
        std::cerr << "api_gateway 启动失败: " << error << '\n';
        return 1;
    }

    httplib::Server server;
    server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        Json::Value body;
        body["code"] = 0;
        body["message"] = "ok";
        body["data"]["status"] = "UP";
        setJsonResponse(response, 200, body);
    });
    server.Get("/healthz", [](const httplib::Request&, httplib::Response& response) {
        Json::Value body;
        body["success"] = true;
        body["service"] = "api_gateway";
        body["status"] = "ok";
        setJsonResponse(response, 200, body);
    });

    const auto handler = [&settings](const httplib::Request& request,
                                     httplib::Response& response) {
        const Downstream* downstream = selectDownstream(settings, request.path);
        if (!downstream) {
            Json::Value body;
            body["success"] = false;
            body["message"] = "gateway route not found";
            setJsonResponse(response, 404, body);
            return;
        }
        forwardToDownstream(settings, *downstream, request, response);
    };

    server.Get(R"(/.*)", handler);
    server.Post(R"(/.*)", handler);

    std::cout << "api_gateway listening on 0.0.0.0:" << settings.port << '\n';
    if (!server.listen("0.0.0.0", settings.port)) {
        std::cerr << "api_gateway failed to listen on port " << settings.port << '\n';
        return 1;
    }
    return 0;
}
