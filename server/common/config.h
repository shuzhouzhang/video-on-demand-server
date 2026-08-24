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

struct RedisSettings {
    bool enabled = false;
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password;
    int sessionTtlSeconds = 86400;
};

struct AuthSettings {
    bool enforceGatewayIdentity = false;
};

struct TranscodeSettings {
    bool enabled = true;
    int workerThreads = 1;
    int pollIntervalMs = 500;
    int leaseSeconds = 300;
    int maxAttempts = 3;
    int retryDelaySeconds = 30;
    std::string ffmpegPath = "ffmpeg";
    std::string uploadRoot = "uploads";
    std::string outputRoot = "uploads/transcoded";
};

struct AppSettings {
    ServerSettings server;
    bitelog::Logsettings log;
    DatabaseSettings database;
    RedisSettings redis;
    AuthSettings auth;
    TranscodeSettings transcode;
};

class Config {
public:
    // 输入配置文件路径；成功返回完整配置，失败返回 nullopt 并填写 error。
    static std::optional<AppSettings> load(const std::string& filename,
                                           std::string& error);
};

}  // namespace biteconfig
