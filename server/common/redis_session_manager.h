#pragma once

#include "config.h"

#include <optional>
#include <string>

struct redisContext;

namespace bitesession {

class RedisSessionManager {
public:
    RedisSessionManager();
    explicit RedisSessionManager(const biteconfig::RedisSettings& settings);
    ~RedisSessionManager();

    RedisSessionManager(const RedisSessionManager&) = delete;
    RedisSessionManager& operator=(const RedisSessionManager&) = delete;

    bool connect(std::string& error);
    bool enabled() const;

    bool createToken(const std::string& account,
                     std::string& token,
                     std::string& error);
    std::optional<std::string> accountForToken(const std::string& token,
                                               std::string& error);
    bool deleteToken(const std::string& token, std::string& error);

private:
    std::string keyForToken(const std::string& token) const;

    biteconfig::RedisSettings settings_;
    redisContext* context_ = nullptr;
};

}  // namespace bitesession
