#include "../common/config.h"
#include "../common/http_client.h"
#include "../common/redis_session_manager.h"
#include "../common/util.h"

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
    Downstream file{"file_service", "http://127.0.0.1:9104"};
    Downstream transcode{"transcode_service", "http://127.0.0.1:10004"};
    int timeoutMs = 3000;
    biteconfig::RedisSettings redis;
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
    loadUrl("file_service", settings.file);
    loadUrl("transcode_service", settings.transcode);
    if ((*root)["timeout_ms"].isInt() && (*root)["timeout_ms"].asInt() > 0) {
        settings.timeoutMs = (*root)["timeout_ms"].asInt();
    }
    const Json::Value& redis = (*root)["redis"];
    if (redis.isObject()) {
        if (redis["enabled"].isBool()) {
            settings.redis.enabled = redis["enabled"].asBool();
        }
        if (redis["host"].isString() && !redis["host"].asString().empty()) {
            settings.redis.host = redis["host"].asString();
        }
        if (redis["port"].isInt() && redis["port"].asInt() > 0 &&
            redis["port"].asInt() <= 65535) {
            settings.redis.port =
                static_cast<std::uint16_t>(redis["port"].asInt());
        }
        if (redis["password"].isString()) {
            settings.redis.password = redis["password"].asString();
        }
        if (redis["session_ttl_seconds"].isInt() &&
            redis["session_ttl_seconds"].asInt() > 0) {
            settings.redis.sessionTtlSeconds =
                redis["session_ttl_seconds"].asInt();
        }
    }
    return true;
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
        return &settings.video;
    }
    if (path == "/files/upload" || path.rfind("/uploads/", 0) == 0) {
        return &settings.file;
    }
    if (path == "/transcode/jobs") {
        return &settings.transcode;
    }
    if (path == "/videos" || path == "/videos/detail" ||
        path == "/videos/search" || path == "/videos/play-url" ||
        path == "/videos/upload" || path == "/users/videos" ||
        path == "/admin/reviews" || path == "/admin/reviews/action" ||
        path == "/__smoke-cleanup") {
        return &settings.video;
    }
    return nullptr;
}

std::string bearerToken(const httplib::Request& request) {
    if (!request.has_header("Authorization")) {
        return "";
    }
    std::string value = request.get_header_value("Authorization");
    constexpr char prefix[] = "Bearer ";
    if (value.rfind(prefix, 0) == 0) {
        value.erase(0, sizeof(prefix) - 1);
    }
    return value;
}

bool requiresAuth(const std::string& method, const std::string& path) {
    if (path == "/login" || path == "/login/password" ||
        path == "/login/email-code" || path == "/login/email" ||
        path == "/health" || path == "/healthz") {
        return false;
    }
    if (method == "GET" &&
        (path == "/videos" || path == "/videos/detail" ||
         path == "/videos/search" || path == "/videos/play-url" ||
         path.rfind("/uploads/", 0) == 0)) {
        return false;
    }
    return true;
}

bool verifyTokenIfNeeded(bitesession::RedisSessionManager& sessions,
                         const httplib::Request& request,
                         httplib::Response& response) {
    if (!sessions.enabled() || !requiresAuth(request.method, request.path)) {
        return true;
    }

    Json::Value body;
    const std::string token = bearerToken(request);
    std::string error;
    const auto account = sessions.accountForToken(token, error);
    if (!error.empty()) {
        body["success"] = false;
        body["message"] = "token verification unavailable";
        setJsonResponse(response, 502, body);
        return false;
    }
    if (!account || account->empty()) {
        body["success"] = false;
        body["message"] = "unauthorized";
        setJsonResponse(response, 401, body);
        return false;
    }
    return true;
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
    const bitehttp::HttpClient client(downstream.baseUrl, settings.timeoutMs);

    httplib::Headers headers = request.headers;
    headers.erase("Host");
    headers.erase("Content-Length");
    headers.erase("Content-Type");
    headers.erase("Transfer-Encoding");
    headers.erase("X-Request-Id");
    headers.emplace("X-Request-Id", requestId);

    if (request.method != "GET" && request.method != "POST") {
        Json::Value body;
        body["success"] = false;
        body["message"] = "gateway method not allowed";
        setJsonResponse(response, 405, body);
        return;
    }

    bitehttp::DownstreamResponse downstreamResponse;
    std::string error;
    if (!client.send(request, target, headers, downstreamResponse, error)) {
        Json::Value body;
        body["success"] = false;
        body["message"] = "downstream service unavailable";
        logGatewayError(requestId, request.path, downstream,
                        client.endpoint(target), error);
        setJsonResponse(response, 502, body);
        return;
    }

    response.status = downstreamResponse.status;
    response.headers = downstreamResponse.headers;
    response.set_header("X-Request-Id", requestId);
    response.set_content(downstreamResponse.body,
                         downstreamResponse.contentType.empty()
                             ? "application/octet-stream"
                             : downstreamResponse.contentType.c_str());
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

    bitesession::RedisSessionManager sessions(settings.redis);
    if (!sessions.connect(error)) {
        std::cerr << "api_gateway Redis 连接失败: " << error << '\n';
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

    const auto handler = [&settings, &sessions](const httplib::Request& request,
                                                httplib::Response& response) {
        if (!verifyTokenIfNeeded(sessions, request, response)) {
            return;
        }
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
