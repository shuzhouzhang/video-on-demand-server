#include "service_registry.h"

#include "util.h"

#include <jsoncpp/json/json.h>

#include <cstdint>
#include <algorithm>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace bitesvc {

struct ServiceRegistry::State {
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::vector<ServiceEndpoint>> services;
    mutable std::unordered_map<std::string, std::size_t> nextIndex;
};

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

ServiceRegistry::ServiceRegistry() : state_(std::make_shared<State>()) {}

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
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->services[name] = {ServiceEndpoint{name, baseUrl, "static", "http"}};
    state_->nextIndex[name] = 0;
    return true;
}

bool ServiceRegistry::replaceServiceInstances(
    const std::string& name,
    std::vector<ServiceEndpoint> endpoints,
    std::string& error) {
    error.clear();
    if (name.empty()) {
        error = "服务名不能为空";
        return false;
    }
    for (auto& endpoint : endpoints) {
        if (endpoint.name.empty()) endpoint.name = name;
        if (endpoint.name != name || !isValidUrl(endpoint.baseUrl)) {
            error = "无效的动态服务实例: " + name;
            return false;
        }
    }
    std::sort(endpoints.begin(), endpoints.end(),
              [](const ServiceEndpoint& left, const ServiceEndpoint& right) {
                  return left.instanceId < right.instanceId;
              });
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->services[name] = std::move(endpoints);
    state_->nextIndex[name] = 0;
    return true;
}

std::optional<ServiceEndpoint> ServiceRegistry::resolve(
    const std::string& name) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto it = state_->services.find(name);
    if (it == state_->services.end() || it->second.empty()) {
        return std::nullopt;
    }
    std::size_t& index = state_->nextIndex[name];
    const ServiceEndpoint endpoint = it->second[index % it->second.size()];
    index = (index + 1) % it->second.size();
    return endpoint;
}

const ServiceEndpoint* ServiceRegistry::find(const std::string& name) const {
    thread_local ServiceEndpoint snapshot;
    const auto endpoint = resolve(name);
    if (!endpoint) return nullptr;
    snapshot = *endpoint;
    return &snapshot;
}

std::vector<ServiceEndpoint> ServiceRegistry::list() const {
    std::vector<ServiceEndpoint> endpoints;
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (const auto& item : state_->services) {
        endpoints.insert(endpoints.end(), item.second.begin(), item.second.end());
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
    const Json::Value& registryConfig = (*root)["registry"];
    if (registryConfig.isObject()) {
        if (registryConfig["enabled"].isBool()) {
            settings.registrySettings.enabled =
                registryConfig["enabled"].asBool();
        }
        if (registryConfig["endpoint"].isString() &&
            !registryConfig["endpoint"].asString().empty()) {
            settings.registrySettings.endpoint =
                registryConfig["endpoint"].asString();
        }
        if (registryConfig["prefix"].isString() &&
            !registryConfig["prefix"].asString().empty()) {
            settings.registrySettings.prefix =
                registryConfig["prefix"].asString();
        }
        if (registryConfig["refresh_interval_ms"].isInt() &&
            registryConfig["refresh_interval_ms"].asInt() > 0) {
            settings.registrySettings.refreshIntervalMs =
                registryConfig["refresh_interval_ms"].asInt();
        }
    }
    const Json::Value& rpc = (*root)["rpc"];
    if (rpc.isObject()) {
        if (rpc["enabled"].isBool()) {
            settings.rpc.enabled = rpc["enabled"].asBool();
        }
        if (rpc["timeout_ms"].isInt() && rpc["timeout_ms"].asInt() > 0) {
            settings.rpc.timeoutMs = rpc["timeout_ms"].asInt();
        }
        if (rpc["file_timeout_ms"].isInt() &&
            rpc["file_timeout_ms"].asInt() > 0) {
            settings.rpc.fileTimeoutMs = rpc["file_timeout_ms"].asInt();
        }
    }
    loadRedis((*root)["redis"], settings.redis);
    settings.registry = std::move(registry);
    return true;
}

}  // namespace bitesvc
