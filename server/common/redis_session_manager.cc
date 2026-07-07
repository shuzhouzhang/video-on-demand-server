#include "redis_session_manager.h"

#include "util.h"

#include <hiredis/hiredis.h>

#include <stdexcept>
#include <string>

namespace bitesession {

RedisSessionManager::RedisSessionManager() = default;

RedisSessionManager::RedisSessionManager(
    const biteconfig::RedisSettings& settings)
    : settings_(settings) {
}

RedisSessionManager::~RedisSessionManager() {
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
}

bool RedisSessionManager::connect(std::string& error) {
    error.clear();
    if (!settings_.enabled) {
        return true;
    }

    context_ = redisConnect(settings_.host.c_str(), settings_.port);
    if (!context_ || context_->err) {
        error = context_ ? context_->errstr : "redis context allocation failed";
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
        return false;
    }

    if (!settings_.password.empty()) {
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(context_, "AUTH %s", settings_.password.c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            error = reply ? reply->str : "redis AUTH failed";
            if (reply) {
                freeReplyObject(reply);
            }
            redisFree(context_);
            context_ = nullptr;
            return false;
        }
        freeReplyObject(reply);
    }

    redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "PING"));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        error = reply ? reply->str : "redis PING failed";
        if (reply) {
            freeReplyObject(reply);
        }
        redisFree(context_);
        context_ = nullptr;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool RedisSessionManager::enabled() const {
    return settings_.enabled && context_ != nullptr;
}

bool RedisSessionManager::createToken(const std::string& account,
                                      std::string& token,
                                      std::string& error) {
    error.clear();
    if (!enabled()) {
        token.clear();
        return true;
    }

    token = "vod-" + biteutil::Random::code(32, biteutil::UuidType::MIX);
    const std::string key = keyForToken(token);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(context_, "SETEX %s %d %s", key.c_str(),
                     settings_.sessionTtlSeconds, account.c_str()));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        error = reply ? reply->str : "redis SETEX failed";
        if (reply) {
            freeReplyObject(reply);
        }
        token.clear();
        return false;
    }
    freeReplyObject(reply);
    return true;
}

std::optional<std::string> RedisSessionManager::accountForToken(
    const std::string& token,
    std::string& error) {
    error.clear();
    if (!enabled()) {
        return std::nullopt;
    }
    if (token.empty()) {
        return std::nullopt;
    }

    const std::string key = keyForToken(token);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(context_, "GET %s", key.c_str()));
    if (!reply) {
        error = "redis GET failed";
        return std::nullopt;
    }
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        return std::nullopt;
    }
    if (reply->type == REDIS_REPLY_ERROR || reply->type != REDIS_REPLY_STRING) {
        error = reply->str ? reply->str : "redis GET returned invalid type";
        freeReplyObject(reply);
        return std::nullopt;
    }
    std::string account(reply->str, static_cast<std::size_t>(reply->len));
    freeReplyObject(reply);
    return account;
}

bool RedisSessionManager::deleteToken(const std::string& token,
                                      std::string& error) {
    error.clear();
    if (!enabled() || token.empty()) {
        return true;
    }

    const std::string key = keyForToken(token);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(context_, "DEL %s", key.c_str()));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        error = reply ? reply->str : "redis DEL failed";
        if (reply) {
            freeReplyObject(reply);
        }
        return false;
    }
    freeReplyObject(reply);
    return true;
}

std::string RedisSessionManager::keyForToken(const std::string& token) const {
    return "vod:session:" + token;
}

}  // namespace bitesession
