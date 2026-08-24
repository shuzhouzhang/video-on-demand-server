#pragma once

#include "config.h"

#include <memory>
#include <optional>
#include <string>

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
    struct Impl;

    biteconfig::RedisSettings settings_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bitesession
