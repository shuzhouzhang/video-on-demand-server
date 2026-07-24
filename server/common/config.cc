#include "config.h"

#include "util.h"

#include <limits>

namespace biteconfig {

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
        transcodeSettings};
    return settings;
}

}  // namespace biteconfig
