/*
 * 服务配置：从 JSON 文件加载并校验服务端与日志参数。
 */
#pragma once

#include "bitelog.h"

#include <cstdint>
#include <optional>
#include <string>

namespace biteconfig {

struct ServerSettings {
    std::uint16_t port;
};

struct DatabaseSettings {
    std::string host;
    std::uint16_t port;
    std::string user;
    std::string password;
    std::string name;
};

struct AppSettings {
    ServerSettings server;
    bitelog::Logsettings log;
    DatabaseSettings database;
};

class Config {
public:
    // 输入配置文件路径；成功返回完整配置，失败返回 nullopt 并填写 error。
    static std::optional<AppSettings> load(const std::string& filename,
                                           std::string& error);
};

}  // namespace biteconfig
