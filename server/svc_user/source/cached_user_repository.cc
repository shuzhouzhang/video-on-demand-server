#include "cached_user_repository.h"

#include "../../common/bitelog.h"
#include "../../common/util.h"

#include <hiredis/hiredis.h>

#include <string>

namespace svc_user {
namespace {

std::string profileCacheKey(const std::string& account) {
    return "vod:cache:user-profile:" + account;
}

void warnCacheFailure(const std::string& operation,
                      const std::string& detail) {
    if (bitelog::g_logger) {
        WRN("user profile cache {} failed: {}", operation, detail);
    }
}

}  // namespace

RedisCachedUserRepository::RedisCachedUserRepository(
    biterepo::IUserRepository& inner,
    const biteconfig::RedisSettings& settings)
    : inner_(inner), settings_(settings) {}

RedisCachedUserRepository::~RedisCachedUserRepository() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
}

bool RedisCachedUserRepository::connect(std::string& error) {
    error.clear();
    if (!settings_.enabled) return true;

    std::lock_guard<std::mutex> lock(mutex_);
    context_ = redisConnect(settings_.host.c_str(), settings_.port);
    if (!context_ || context_->err) {
        error = context_ ? context_->errstr :
            "redis context allocation failed";
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
            error = reply && reply->str ? reply->str : "redis AUTH failed";
            if (reply) freeReplyObject(reply);
            redisFree(context_);
            context_ = nullptr;
            return false;
        }
        freeReplyObject(reply);
    }

    redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "PING"));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        error = reply && reply->str ? reply->str : "redis PING failed";
        if (reply) freeReplyObject(reply);
        redisFree(context_);
        context_ = nullptr;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool RedisCachedUserRepository::enabled() const {
    return settings_.enabled && context_ != nullptr;
}

bool RedisCachedUserRepository::loadCachedProfile(
    const std::string& account,
    bitevideo::UserProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled()) return false;

    const std::string key = profileCacheKey(account);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(context_, "GET %b", key.data(), key.size()));
    if (!reply) {
        warnCacheFailure("GET", "no reply");
        return false;
    }
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        return false;
    }
    if (reply->type != REDIS_REPLY_STRING) {
        const std::string detail = reply->str ? reply->str : "invalid reply";
        freeReplyObject(reply);
        warnCacheFailure("GET", detail);
        return false;
    }

    const std::string payload(reply->str, static_cast<std::size_t>(reply->len));
    freeReplyObject(reply);
    const auto json = biteutil::JSON::unserialize(payload);
    if (!json || !json->isObject() ||
        !(*json)["account"].isString() ||
        !(*json)["userName"].isString() ||
        !(*json)["description"].isString() ||
        !(*json)["avatarPath"].isString() ||
        (*json)["account"].asString() != account) {
        redisReply* deleteReply = static_cast<redisReply*>(
            redisCommand(context_, "DEL %b", key.data(), key.size()));
        if (deleteReply) freeReplyObject(deleteReply);
        warnCacheFailure("decode", "discarded malformed entry");
        return false;
    }

    profile.account = (*json)["account"].asString();
    profile.userName = (*json)["userName"].asString();
    profile.description = (*json)["description"].asString();
    profile.avatarPath = (*json)["avatarPath"].asString();
    return true;
}

void RedisCachedUserRepository::storeCachedProfile(
    const bitevideo::UserProfile& profile) {
    Json::Value json;
    json["account"] = profile.account;
    json["userName"] = profile.userName;
    json["description"] = profile.description;
    json["avatarPath"] = profile.avatarPath;
    const auto payload = biteutil::JSON::serialize(json);
    if (!payload) {
        warnCacheFailure("encode", "JSON serialization failed");
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled()) return;
    const std::string key = profileCacheKey(profile.account);
    const int baseTtl = settings_.profileCacheTtlSeconds;
    const int ttl = baseTtl + static_cast<int>(
        biteutil::Random::number(0, static_cast<std::size_t>(baseTtl)));
    redisReply* reply = static_cast<redisReply*>(redisCommand(
        context_, "SETEX %b %d %b", key.data(), key.size(), ttl,
        payload->data(), payload->size()));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        const std::string detail = reply && reply->str ? reply->str : "no reply";
        if (reply) freeReplyObject(reply);
        warnCacheFailure("SETEX", detail);
        return;
    }
    freeReplyObject(reply);
}

void RedisCachedUserRepository::invalidateProfile(
    const std::string& account) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled()) return;
    const std::string key = profileCacheKey(account);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(context_, "DEL %b", key.data(), key.size()));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        const std::string detail = reply && reply->str ? reply->str : "no reply";
        if (reply) freeReplyObject(reply);
        warnCacheFailure("DEL", detail);
        return;
    }
    freeReplyObject(reply);
}

bool RedisCachedUserRepository::userProfile(
    const std::string& account,
    std::optional<bitevideo::UserProfile>& profile,
    std::string& error) {
    bitevideo::UserProfile cached;
    if (loadCachedProfile(account, cached)) {
        profile = std::move(cached);
        error.clear();
        return true;
    }
    if (!inner_.userProfile(account, profile, error)) return false;
    if (profile) storeCachedProfile(*profile);
    return true;
}

bool RedisCachedUserRepository::updateUserProfile(
    const std::string& account,
    const std::string& userName,
    const std::string& description,
    std::optional<bitevideo::UserProfile>& profile,
    std::string& error) {
    const bool succeeded = inner_.updateUserProfile(
        account, userName, description, profile, error);
    if (succeeded) invalidateProfile(account);
    return succeeded;
}

bool RedisCachedUserRepository::updateAvatarPath(
    const std::string& account,
    const std::string& avatarPath,
    bool& updated,
    std::string& error) {
    const bool succeeded = inner_.updateAvatarPath(
        account, avatarPath, updated, error);
    if (succeeded && updated) invalidateProfile(account);
    return succeeded;
}

bool RedisCachedUserRepository::passwordLogin(
    const std::string& account,
    const std::string& password,
    std::optional<bitevideo::UserProfile>& profile,
    std::string& error) {
    const bool succeeded = inner_.passwordLogin(account, password, profile, error);
    if (succeeded && profile) storeCachedProfile(*profile);
    return succeeded;
}

bool RedisCachedUserRepository::createEmailCode(
    const std::string& email,
    bitevideo::EmailCodeSession& session,
    std::string& error) {
    return inner_.createEmailCode(email, session, error);
}

bool RedisCachedUserRepository::emailLogin(
    const std::string& email,
    const std::string& authcodeId,
    const std::string& authcode,
    std::optional<bitevideo::UserProfile>& profile,
    std::string& error) {
    const bool succeeded = inner_.emailLogin(
        email, authcodeId, authcode, profile, error);
    if (succeeded && profile) storeCachedProfile(*profile);
    return succeeded;
}

bool RedisCachedUserRepository::logout(const std::string& account,
                                       bool& knownUser,
                                       std::string& error) {
    return inner_.logout(account, knownUser, error);
}

}  // namespace svc_user
