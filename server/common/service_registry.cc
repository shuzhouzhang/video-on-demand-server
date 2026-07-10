#include "service_registry.h"

#include "util.h"

#include <jsoncpp/json/json.h>

#include <cstdint>
#include <optional>

namespace bitesvc {
namespace {

bool isValidUrl(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

void loadRedis(const Json::Value& redis, biteconfig::RedisSettings& settings) {
    if (!redis.isObject()) {
        return;
    }
    if (redis["enabled"].isBool()) {
        settings.enabled = redis["enabled"].asBool();
    }
    if (redis["host"].isString() && !redis["host"].asString().empty()) {
        settings.host = redis["host"].asString();
    }
    if (redis["port"].isInt() && redis["port"].asInt() > 0 &&
        redis["port"].asInt() <= 65535) {
        settings.port = static_cast<std::uint16_t>(redis["port"].asInt());
    }
    if (redis["password"].isString()) {
        settings.password = redis["password"].asString();
    }
    if (redis["session_ttl_seconds"].isInt() &&
        redis["session_ttl_seconds"].asInt() > 0) {
        settings.sessionTtlSeconds = redis["session_ttl_seconds"].asInt();
    }
}

}  // namespace

bool ServiceRegistry::registerService(const std::string& name,
                                      const std::string& baseUrl,
                                      std::string& error) {
    error.clear();
    if (name.empty()) {
        error = "服务名不能为空";
        return false;
    }
    if (!isValidUrl(baseUrl)) {
        error = "服务地址必须以 http:// 或 https:// 开头: " + name;
        return false;
    }
    services_[name] = ServiceEndpoint{name, baseUrl};
    return true;
}

const ServiceEndpoint* ServiceRegistry::find(const std::string& name) const {
    const auto it = services_.find(name);
    return it == services_.end() ? nullptr : &it->second;
}

std::vector<ServiceEndpoint> ServiceRegistry::list() const {
    std::vector<ServiceEndpoint> endpoints;
    endpoints.reserve(services_.size());
    for (const auto& item : services_) {
        endpoints.push_back(item.second);
    }
    return endpoints;
}

bool loadDiscoverySettings(const std::string& filename,
                           DiscoverySettings& settings,
                           std::string& error) {
    error.clear();
    std::string body;
    if (!biteutil::FUTIL::read(filename, body)) {
        error = "无法读取服务发现配置文件: " + filename;
        return false;
    }
    const auto root = biteutil::JSON::unserialize(body);
    if (!root || !root->isObject()) {
        error = "服务发现配置不是有效 JSON 对象: " + filename;
        return false;
    }

    ServiceRegistry registry;
    const Json::Value& services = (*root)["services"].isObject()
        ? (*root)["services"] : *root;
    const char* required[] = {
        "user_service", "video_service", "file_service", "transcode_service"};
    for (const char* name : required) {
        const Json::Value& value = services[name];
        if (!value.isString() || value.asString().empty()) {
            error = std::string("缺少服务地址: ") + name;
            return false;
        }
        if (!registry.registerService(name, value.asString(), error)) {
            return false;
        }
    }

    if ((*root)["timeout_ms"].isInt() && (*root)["timeout_ms"].asInt() > 0) {
        settings.timeoutMs = (*root)["timeout_ms"].asInt();
    }
    loadRedis((*root)["redis"], settings.redis);
    settings.registry = std::move(registry);
    return true;
}

}  // namespace bitesvc
