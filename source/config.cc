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

    AppSettings settings{
        {static_cast<std::uint16_t>(port.asInt())},
        {log["async"].asBool(), log["level"].asInt(),
         log["pattern"].asString(), log["path"].asString()},
        {database["host"].asString(),
         static_cast<std::uint16_t>(database["port"].asInt()),
         database["user"].asString(), database["password"].asString(),
         database["name"].asString()}};
    return settings;
}

}  // namespace biteconfig
