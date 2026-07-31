#include "config.h"

#include "util.h"

#include <limits>

namespace biteconfig {

namespace {

bool readOptionalBool(const Json::Value& object, const char* key,
                      bool& destination, std::string& error,
                      const char* section) {
    const Json::Value& value = object[key];
    if (value.isNull()) return true;
    if (!value.isBool()) {
        error = std::string(section) + "." + key + " must be a boolean";
        return false;
    }
    destination = value.asBool();
    return true;
}

bool readOptionalString(const Json::Value& object, const char* key,
                        std::string& destination, std::string& error,
                        const char* section, bool allowEmpty = false) {
    const Json::Value& value = object[key];
    if (value.isNull()) return true;
    if (!value.isString() || (!allowEmpty && value.asString().empty())) {
        error = std::string(section) + "." + key +
            (allowEmpty ? " must be a string" : " must be a non-empty string");
        return false;
    }
    destination = value.asString();
    return true;
}

bool readOptionalPositiveInt(const Json::Value& object, const char* key,
                             int& destination, int maximum,
                             std::string& error, const char* section) {
    const Json::Value& value = object[key];
    if (value.isNull()) return true;
    if (!value.isInt() || value.asInt() < 1 || value.asInt() > maximum) {
        error = std::string(section) + "." + key +
            " must be a positive integer";
        return false;
    }
    destination = value.asInt();
    return true;
}

bool requireObjectOrNull(const Json::Value& value, const char* section,
                         std::string& error) {
    if (value.isNull() || value.isObject()) return true;
    error = std::string(section) + " must be an object";
    return false;
}

}  // namespace

std::optional<AppSettings> Config::load(const std::string& filename,
                                        std::string& error) {
    error.clear();

    std::string body;
    if (!biteutil::FUTIL::read(filename, body)) {
        error = "无法读取配置文件: " + filename;
        return std::nullopt;
    }

    const auto root = biteutil::JSON::unserialize(body);
    if (!root || !root->isObject()) {
        error = "配置文件不是有效的 JSON 对象";
        return std::nullopt;
    }

    const Json::Value& server = (*root)["server"];
    const Json::Value& log = (*root)["log"];
    const Json::Value& database = (*root)["database"];
    if (!server.isObject() || !log.isObject() || !database.isObject()) {
        error = "配置必须包含 server、log 和 database 对象";
        return std::nullopt;
    }

    const Json::Value& port = server["port"];
    if (!port.isInt() || port.asInt() < 1 ||
        port.asInt() > std::numeric_limits<std::uint16_t>::max()) {
        error = "server.port 必须是 1 到 65535 之间的整数";
        return std::nullopt;
    }

    if (!log["async"].isBool()) {
        error = "log.async 必须是布尔值";
        return std::nullopt;
    }
    if (!log["level"].isInt() || log["level"].asInt() < 0 ||
        log["level"].asInt() > 6) {
        error = "log.level 必须是 0 到 6 之间的整数";
        return std::nullopt;
    }
    if (!log["pattern"].isString() || log["pattern"].asString().empty()) {
        error = "log.pattern 必须是非空字符串";
        return std::nullopt;
    }
    if (!log["path"].isString() || log["path"].asString().empty()) {
        error = "log.path 必须是非空字符串";
        return std::nullopt;
    }

    if (!database["host"].isString() ||
        database["host"].asString().empty()) {
        error = "database.host 必须是非空字符串";
        return std::nullopt;
    }
    if (!database["port"].isInt() || database["port"].asInt() < 1 ||
        database["port"].asInt() >
            std::numeric_limits<std::uint16_t>::max()) {
        error = "database.port 必须是 1 到 65535 之间的整数";
        return std::nullopt;
    }
    if (!database["user"].isString() ||
        database["user"].asString().empty()) {
        error = "database.user 必须是非空字符串";
        return std::nullopt;
    }
    if (!database["password"].isString()) {
        error = "database.password 必须是字符串";
        return std::nullopt;
    }
    if (!database["name"].isString() ||
        database["name"].asString().empty()) {
        error = "database.name 必须是非空字符串";
        return std::nullopt;
    }

    RedisSettings redisSettings;
    const Json::Value& redis = (*root)["redis"];
    if (!redis.isNull()) {
        if (!redis.isObject()) {
            error = "redis 必须是对象";
            return std::nullopt;
        }
        if (redis["enabled"].isBool()) {
            redisSettings.enabled = redis["enabled"].asBool();
        }
        if (redis["host"].isString() && !redis["host"].asString().empty()) {
            redisSettings.host = redis["host"].asString();
        }
        if (redis["port"].isInt() && redis["port"].asInt() >= 1 &&
            redis["port"].asInt() <=
                std::numeric_limits<std::uint16_t>::max()) {
            redisSettings.port = static_cast<std::uint16_t>(redis["port"].asInt());
        }
        if (redis["password"].isString()) {
            redisSettings.password = redis["password"].asString();
        }
        if (redis["session_ttl_seconds"].isInt() &&
            redis["session_ttl_seconds"].asInt() > 0) {
            redisSettings.sessionTtlSeconds =
                redis["session_ttl_seconds"].asInt();
        }
        if (!redis["profile_cache_ttl_seconds"].isNull()) {
            if (!redis["profile_cache_ttl_seconds"].isInt() ||
                redis["profile_cache_ttl_seconds"].asInt() < 1 ||
                redis["profile_cache_ttl_seconds"].asInt() > 86400) {
                error = "redis.profile_cache_ttl_seconds must be between 1 and 86400";
                return std::nullopt;
            }
            redisSettings.profileCacheTtlSeconds =
                redis["profile_cache_ttl_seconds"].asInt();
        }
    }

    AuthSettings authSettings;
    const Json::Value& auth = (*root)["auth"];
    if (!auth.isNull()) {
        if (!auth.isObject()) {
            error = "auth must be an object";
            return std::nullopt;
        }
        if (!auth["enforce_gateway_identity"].isNull() &&
            !auth["enforce_gateway_identity"].isBool()) {
            error = "auth.enforce_gateway_identity must be a boolean";
            return std::nullopt;
        }
        if (auth["enforce_gateway_identity"].isBool()) {
            authSettings.enforceGatewayIdentity =
                auth["enforce_gateway_identity"].asBool();
        }
    }

    RpcSettings rpcSettings;
    const Json::Value& rpc = (*root)["rpc"];
    int rpcPort = rpcSettings.port;
    if (!requireObjectOrNull(rpc, "rpc", error) ||
        !readOptionalBool(rpc, "enabled", rpcSettings.enabled, error, "rpc") ||
        !readOptionalString(rpc, "bind_host", rpcSettings.bindHost, error, "rpc") ||
        !readOptionalPositiveInt(rpc, "port", rpcPort, 65535, error, "rpc") ||
        !readOptionalPositiveInt(rpc, "timeout_ms", rpcSettings.timeoutMs,
                                 60000, error, "rpc") ||
        !readOptionalPositiveInt(rpc, "file_timeout_ms",
                                 rpcSettings.fileTimeoutMs, 600000,
                                 error, "rpc")) {
        return std::nullopt;
    }
    rpcSettings.port = rpcPort;

    RegistrySettings registrySettings;
    const Json::Value& registry = (*root)["registry"];
    if (!requireObjectOrNull(registry, "registry", error) ||
        !readOptionalBool(registry, "enabled", registrySettings.enabled,
                          error, "registry") ||
        !readOptionalString(registry, "endpoint", registrySettings.endpoint,
                            error, "registry") ||
        !readOptionalString(registry, "prefix", registrySettings.prefix,
                            error, "registry") ||
        !readOptionalPositiveInt(registry, "lease_ttl_seconds",
                                 registrySettings.leaseTtlSeconds, 300,
                                 error, "registry") ||
        !readOptionalPositiveInt(registry, "keepalive_seconds",
                                 registrySettings.keepAliveSeconds, 60,
                                 error, "registry") ||
        !readOptionalPositiveInt(registry, "refresh_interval_ms",
                                 registrySettings.refreshIntervalMs, 60000,
                                 error, "registry")) {
        return std::nullopt;
    }
    if (registrySettings.keepAliveSeconds >= registrySettings.leaseTtlSeconds) {
        error = "registry.keepalive_seconds must be smaller than lease_ttl_seconds";
        return std::nullopt;
    }

    RabbitMqSettings rabbitMqSettings;
    const Json::Value& rabbitmq = (*root)["rabbitmq"];
    int rabbitPort = rabbitMqSettings.port;
    if (!requireObjectOrNull(rabbitmq, "rabbitmq", error) ||
        !readOptionalBool(rabbitmq, "enabled", rabbitMqSettings.enabled,
                          error, "rabbitmq") ||
        !readOptionalString(rabbitmq, "host", rabbitMqSettings.host,
                            error, "rabbitmq") ||
        !readOptionalPositiveInt(rabbitmq, "port", rabbitPort, 65535,
                                 error, "rabbitmq") ||
        !readOptionalString(rabbitmq, "user", rabbitMqSettings.user,
                            error, "rabbitmq") ||
        !readOptionalString(rabbitmq, "password", rabbitMqSettings.password,
                            error, "rabbitmq", true) ||
        !readOptionalString(rabbitmq, "password_file",
                            rabbitMqSettings.passwordFile, error,
                            "rabbitmq", true) ||
        !readOptionalString(rabbitmq, "virtual_host",
                            rabbitMqSettings.virtualHost, error, "rabbitmq") ||
        !readOptionalPositiveInt(rabbitmq, "max_attempts",
                                 rabbitMqSettings.maxAttempts, 20,
                                 error, "rabbitmq")) {
        return std::nullopt;
    }
    rabbitMqSettings.port = static_cast<std::uint16_t>(rabbitPort);

    ElasticsearchSettings elasticsearchSettings;
    const Json::Value& elasticsearch = (*root)["elasticsearch"];
    if (!requireObjectOrNull(elasticsearch, "elasticsearch", error) ||
        !readOptionalBool(elasticsearch, "enabled",
                          elasticsearchSettings.enabled, error,
                          "elasticsearch") ||
        !readOptionalString(elasticsearch, "endpoint",
                            elasticsearchSettings.endpoint, error,
                            "elasticsearch") ||
        !readOptionalString(elasticsearch, "index_alias",
                            elasticsearchSettings.indexAlias, error,
                            "elasticsearch") ||
        !readOptionalPositiveInt(elasticsearch, "timeout_ms",
                                 elasticsearchSettings.timeoutMs, 60000,
                                 error, "elasticsearch")) {
        return std::nullopt;
    }

    FastDfsSettings fastDfsSettings;
    const Json::Value& fastdfs = (*root)["fastdfs"];
    if (!requireObjectOrNull(fastdfs, "fastdfs", error) ||
        !readOptionalBool(fastdfs, "enabled", fastDfsSettings.enabled,
                          error, "fastdfs") ||
        !readOptionalString(fastdfs, "client_config",
                            fastDfsSettings.clientConfig, error, "fastdfs") ||
        !readOptionalString(fastdfs, "public_path_prefix",
                            fastDfsSettings.publicPathPrefix, error,
                            "fastdfs")) {
        return std::nullopt;
    }

    TranscodeSettings transcodeSettings;
    const Json::Value& transcode = (*root)["transcode"];
    if (!transcode.isNull()) {
        if (!transcode.isObject()) {
            error = "transcode must be an object";
            return std::nullopt;
        }
        const auto readPositiveInt = [&](const char* key, int& destination,
                                         int maximum) {
            const Json::Value& value = transcode[key];
            if (value.isNull()) return true;
            if (!value.isInt() || value.asInt() < 1 ||
                value.asInt() > maximum) {
                error = std::string("transcode.") + key +
                    " must be a positive integer";
                return false;
            }
            destination = value.asInt();
            return true;
        };
        if (!transcode["enabled"].isNull()) {
            if (!transcode["enabled"].isBool()) {
                error = "transcode.enabled must be a boolean";
                return std::nullopt;
            }
            transcodeSettings.enabled = transcode["enabled"].asBool();
        }
        if (!readPositiveInt("worker_threads",
                             transcodeSettings.workerThreads, 32) ||
            !readPositiveInt("poll_interval_ms",
                             transcodeSettings.pollIntervalMs, 60000) ||
            !readPositiveInt("lease_seconds",
                             transcodeSettings.leaseSeconds, 86400) ||
            !readPositiveInt("max_attempts",
                             transcodeSettings.maxAttempts, 20) ||
            !readPositiveInt("retry_delay_seconds",
                             transcodeSettings.retryDelaySeconds, 86400)) {
            return std::nullopt;
        }
        const auto readNonEmptyString = [&](const char* key,
                                            std::string& destination) {
            const Json::Value& value = transcode[key];
            if (value.isNull()) return true;
            if (!value.isString() || value.asString().empty()) {
                error = std::string("transcode.") + key +
                    " must be a non-empty string";
                return false;
            }
            destination = value.asString();
            return true;
        };
        if (!readNonEmptyString("ffmpeg_path", transcodeSettings.ffmpegPath) ||
            !readNonEmptyString("upload_root", transcodeSettings.uploadRoot) ||
            !readNonEmptyString("output_root", transcodeSettings.outputRoot)) {
            return std::nullopt;
        }
    }

    AppSettings settings{
        {static_cast<std::uint16_t>(port.asInt())},
        {log["async"].asBool(), log["level"].asInt(),
         log["pattern"].asString(), log["path"].asString()},
        {database["host"].asString(),
         static_cast<std::uint16_t>(database["port"].asInt()),
         database["user"].asString(), database["password"].asString(),
         database["name"].asString()},
        redisSettings,
        authSettings,
        rpcSettings,
        registrySettings,
        rabbitMqSettings,
        elasticsearchSettings,
        fastDfsSettings,
        transcodeSettings};
    return settings;
}

}  // namespace biteconfig
