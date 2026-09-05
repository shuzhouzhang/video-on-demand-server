#include "etcd_registry.h"

#include "bitelog.h"
#include "util.h"

#include <curl/curl.h>
#include <jsoncpp/json/json.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <utility>

#include <unistd.h>

namespace bitesvc {
namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string& input) {
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    std::uint32_t buffer = 0;
    int bits = -6;
    for (unsigned char value : input) {
        buffer = (buffer << 8) | value;
        bits += 8;
        while (bits >= 0) {
            output.push_back(kBase64Alphabet[(buffer >> bits) & 0x3f]);
            bits -= 6;
        }
    }
    if (bits > -6) output.push_back(kBase64Alphabet[((buffer << 8) >> (bits + 8)) & 0x3f]);
    while (output.size() % 4 != 0) output.push_back('=');
    return output;
}

bool base64Decode(const std::string& input, std::string& output) {
    static const std::vector<int> table = [] {
        std::vector<int> values(256, -1);
        for (int index = 0; index < 64; ++index) {
            values[static_cast<unsigned char>(kBase64Alphabet[index])] = index;
        }
        return values;
    }();
    output.clear();
    std::uint32_t buffer = 0;
    int bits = -8;
    for (unsigned char value : input) {
        if (value == '=') break;
        if (table[value] < 0) return false;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(table[value]);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<char>((buffer >> bits) & 0xff));
            bits -= 8;
        }
    }
    return true;
}

std::string prefixEnd(std::string prefix) {
    for (std::size_t index = prefix.size(); index > 0; --index) {
        unsigned char value = static_cast<unsigned char>(prefix[index - 1]);
        if (value != 0xff) {
            prefix[index - 1] = static_cast<char>(value + 1);
            prefix.resize(index);
            return prefix;
        }
    }
    return std::string(1, '\0');
}

std::size_t appendResponse(char* data, std::size_t size,
                           std::size_t count, void* target) {
    const std::size_t bytes = size * count;
    static_cast<std::string*>(target)->append(data, bytes);
    return bytes;
}

class CurlEtcdTransport final : public IEtcdTransport {
public:
    CurlEtcdTransport(std::string endpoint, int timeoutMs)
        : endpoint_(std::move(endpoint)), timeoutMs_(timeoutMs) {
        while (!endpoint_.empty() && endpoint_.back() == '/') {
            endpoint_.pop_back();
        }
    }

    bool grantLease(int ttlSeconds, std::int64_t& leaseId,
                    std::string& error) override {
        Json::Value request;
        request["TTL"] = ttlSeconds;
        Json::Value response;
        if (!post("/v3/lease/grant", request, response, error)) return false;
        return readInt64(response["ID"], leaseId, "lease ID", error);
    }

    bool keepAlive(std::int64_t leaseId, std::string& error) override {
        Json::Value request;
        request["ID"] = std::to_string(leaseId);
        Json::Value response;
        if (!post("/v3/lease/keepalive", request, response, error)) return false;
        const Json::Value& result = response["result"].isObject()
            ? response["result"] : response;
        std::int64_t confirmedId = 0;
        return readInt64(result["ID"], confirmedId, "keepalive lease ID", error) &&
            confirmedId == leaseId;
    }

    bool put(const std::string& key, const std::string& value,
             std::int64_t leaseId, std::string& error) override {
        Json::Value request;
        request["key"] = base64Encode(key);
        request["value"] = base64Encode(value);
        request["lease"] = std::to_string(leaseId);
        Json::Value response;
        return post("/v3/kv/put", request, response, error);
    }

    bool erase(const std::string& key, std::string& error) override {
        Json::Value request;
        request["key"] = base64Encode(key);
        Json::Value response;
        return post("/v3/kv/deleterange", request, response, error);
    }

    bool listPrefix(
        const std::string& prefix,
        std::vector<std::pair<std::string, std::string>>& values,
        std::string& error) override {
        Json::Value request;
        request["key"] = base64Encode(prefix);
        request["range_end"] = base64Encode(prefixEnd(prefix));
        Json::Value response;
        if (!post("/v3/kv/range", request, response, error)) return false;
        values.clear();
        const Json::Value& kvs = response["kvs"];
        if (kvs.isNull()) return true;
        if (!kvs.isArray()) {
            error = "etcd range response has invalid kvs";
            return false;
        }
        for (const auto& item : kvs) {
            std::string key;
            std::string value;
            if (!item["key"].isString() || !item["value"].isString() ||
                !base64Decode(item["key"].asString(), key) ||
                !base64Decode(item["value"].asString(), value)) {
                error = "etcd range response contains invalid base64";
                return false;
            }
            values.emplace_back(std::move(key), std::move(value));
        }
        return true;
    }

private:
    bool post(const std::string& path, const Json::Value& request,
              Json::Value& response, std::string& error) const {
        static const int curlInitialized = [] {
            return curl_global_init(CURL_GLOBAL_DEFAULT);
        }();
        if (curlInitialized != CURLE_OK) {
            error = "cannot initialize libcurl";
            return false;
        }
        const auto serialized = biteutil::JSON::serialize(request);
        if (!serialized) {
            error = "cannot serialize etcd request";
            return false;
        }
        CURL* handle = curl_easy_init();
        if (!handle) {
            error = "cannot create etcd HTTP client";
            return false;
        }
        std::string body;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        const std::string url = endpoint_ + path;
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, serialized->c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(serialized->size()));
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendResponse);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(timeoutMs_));
        curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                         static_cast<long>(timeoutMs_));
        const CURLcode code = curl_easy_perform(handle);
        long status = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(handle);
        if (code != CURLE_OK) {
            error = std::string("etcd request failed: ") + curl_easy_strerror(code);
            return false;
        }
        if (status < 200 || status >= 300) {
            error = "etcd returned HTTP " + std::to_string(status) + ": " + body;
            return false;
        }
        const auto parsed = biteutil::JSON::unserialize(body.empty() ? "{}" : body);
        if (!parsed || !parsed->isObject()) {
            error = "etcd returned invalid JSON";
            return false;
        }
        response = *parsed;
        if (response["error"].isString()) {
            error = "etcd error: " + response["error"].asString();
            return false;
        }
        return true;
    }

    static bool readInt64(const Json::Value& value, std::int64_t& result,
                          const char* label, std::string& error) {
        try {
            if (value.isString()) {
                result = std::stoll(value.asString());
                return true;
            }
            if (value.isInt64() || value.isUInt64()) {
                result = value.asInt64();
                return true;
            }
        } catch (const std::exception&) {
        }
        error = std::string("etcd response is missing ") + label;
        return false;
    }

    std::string endpoint_;
    int timeoutMs_;
};

std::string normalizePrefix(std::string prefix) {
    while (prefix.size() > 1 && prefix.back() == '/') prefix.pop_back();
    return prefix;
}

std::int64_t nowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

std::unique_ptr<IEtcdTransport> makeCurlEtcdTransport(
    const std::string& endpoint, int timeoutMs) {
    return std::make_unique<CurlEtcdTransport>(endpoint, timeoutMs);
}

EtcdServiceProvider::EtcdServiceProvider(
    biteconfig::RegistrySettings settings,
    std::string serviceName,
    ServiceEndpoint endpoint,
    std::string version)
    : settings_(std::move(settings)),
      serviceName_(std::move(serviceName)),
      endpoint_(std::move(endpoint)),
      version_(std::move(version)),
      transport_(makeCurlEtcdTransport(settings_.endpoint, 2000)) {
    const auto startedAt = nowUnixMs();
    instanceId_ = serviceName_ + "-" + std::to_string(::getpid()) + "-" +
        std::to_string(startedAt);
    endpoint_.name = serviceName_;
    endpoint_.instanceId = instanceId_;
    key_ = normalizePrefix(settings_.prefix) + "/" + serviceName_ + "/" +
        instanceId_;
}

EtcdServiceProvider::~EtcdServiceProvider() { stop(); }

bool EtcdServiceProvider::registerLease(std::string& error) {
    std::int64_t leaseId = 0;
    if (!transport_->grantLease(settings_.leaseTtlSeconds, leaseId, error)) {
        return false;
    }
    Json::Value value;
    value["service_name"] = serviceName_;
    value["instance_id"] = instanceId_;
    value["base_url"] = endpoint_.baseUrl;
    value["protocol"] = endpoint_.protocol;
    value["version"] = version_;
    value["started_at_unix_ms"] = Json::Int64(nowUnixMs());
    const auto serialized = biteutil::JSON::serialize(value);
    if (!serialized || !transport_->put(key_, *serialized, leaseId, error)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(leaseMutex_);
    leaseId_ = leaseId;
    return true;
}

bool EtcdServiceProvider::start(std::string& error) {
    bool expected = true;
    if (!stopped_.compare_exchange_strong(expected, false)) return true;
    if (!registerLease(error)) {
        stopped_.store(true);
        return false;
    }
    worker_ = std::thread(&EtcdServiceProvider::keepAliveLoop, this);
    return true;
}

void EtcdServiceProvider::stop() {
    stopped_.store(true);
    if (worker_.joinable()) worker_.join();
    if (transport_) {
        std::string ignored;
        transport_->erase(key_, ignored);
    }
}

const std::string& EtcdServiceProvider::instanceId() const noexcept {
    return instanceId_;
}

void EtcdServiceProvider::keepAliveLoop() {
    while (!stopped_.load()) {
        std::this_thread::sleep_for(
            std::chrono::seconds(settings_.keepAliveSeconds));
        if (stopped_.load()) break;
        std::int64_t leaseId = 0;
        {
            std::lock_guard<std::mutex> lock(leaseMutex_);
            leaseId = leaseId_;
        }
        std::string error;
        if (!transport_->keepAlive(leaseId, error)) {
            ERR("etcd keepalive failed for {}: {}", instanceId_, error);
            if (!registerLease(error)) {
                ERR("etcd re-registration failed for {}: {}", instanceId_, error);
            }
        }
    }
}

EtcdServiceWatcher::EtcdServiceWatcher(
    biteconfig::RegistrySettings settings,
    ServiceRegistry& registry)
    : settings_(std::move(settings)),
      registry_(registry),
      transport_(makeCurlEtcdTransport(settings_.endpoint, 2000)) {
    for (const auto& endpoint : registry_.list()) {
        knownServices_.insert(endpoint.name);
    }
}

EtcdServiceWatcher::~EtcdServiceWatcher() { stop(); }

bool EtcdServiceWatcher::start(std::string& error) {
    bool expected = true;
    if (!stopped_.compare_exchange_strong(expected, false)) return true;
    if (!refresh(error)) {
        stopped_.store(true);
        return false;
    }
    worker_ = std::thread(&EtcdServiceWatcher::watchLoop, this);
    return true;
}

void EtcdServiceWatcher::stop() {
    stopped_.store(true);
    if (worker_.joinable()) worker_.join();
}

bool EtcdServiceWatcher::refresh(std::string& error) {
    std::vector<std::pair<std::string, std::string>> values;
    const std::string prefix = normalizePrefix(settings_.prefix) + "/";
    if (!transport_->listPrefix(prefix, values, error)) return false;

    std::unordered_map<std::string, std::vector<ServiceEndpoint>> grouped;
    for (const auto& item : values) {
        const auto value = biteutil::JSON::unserialize(item.second);
        if (!value || !value->isObject() ||
            !(*value)["service_name"].isString() ||
            !(*value)["instance_id"].isString() ||
            !(*value)["base_url"].isString()) {
            continue;
        }
        ServiceEndpoint endpoint;
        endpoint.name = (*value)["service_name"].asString();
        endpoint.instanceId = (*value)["instance_id"].asString();
        endpoint.baseUrl = (*value)["base_url"].asString();
        endpoint.protocol = (*value)["protocol"].isString()
            ? (*value)["protocol"].asString() : "http";
        grouped[endpoint.name].push_back(std::move(endpoint));
    }
    for (const auto& item : grouped) knownServices_.insert(item.first);
    for (const auto& service : knownServices_) {
        auto endpoints = grouped.find(service);
        std::vector<ServiceEndpoint> replacement;
        if (endpoints != grouped.end()) replacement = std::move(endpoints->second);
        if (!registry_.replaceServiceInstances(service, std::move(replacement),
                                               error)) {
            return false;
        }
    }
    return true;
}

void EtcdServiceWatcher::watchLoop() {
    while (!stopped_.load()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(settings_.refreshIntervalMs));
        if (stopped_.load()) break;
        std::string error;
        if (!refresh(error)) {
            ERR("etcd service watch refresh failed: {}", error);
        }
    }
}

}  // namespace bitesvc
