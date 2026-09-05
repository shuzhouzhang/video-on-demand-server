#include "cached_user_repository.h"

#include "../../common/bitelog.h"
#include "../../common/util.h"
#include "../../common/session_token.h"

#include <hiredis/hiredis.h>

#include <string>

namespace svc_user {
namespace {

std::string profileCacheKey(const std::string &account) {
    return "vod:cache:user-profile:" + account;
}

void warnCacheFailure(const std::string &operation, const std::string &detail) {
    if (bitelog::g_logger) {
        WRN("user profile cache {} failed: {}", operation, detail);
    }
}

} // namespace

RedisCachedUserRepository::RedisCachedUserRepository(
    biterepo::IUserRepository &inner, const biteconfig::RedisSettings &settings)
    : inner_(inner), settings_(settings) {}

RedisCachedUserRepository::~RedisCachedUserRepository() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
}

bool RedisCachedUserRepository::connect(std::string &error) {
    std::lock_guard<std::mutex> lock(mutex_);
    return connectUnlocked(error);
}

bool RedisCachedUserRepository::connectUnlocked(std::string &error) {
    error.clear();
    if (!settings_.enabled)
        return true;
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
    timeval timeout{1, 0};
    context_ = redisConnectWithTimeout(settings_.host.c_str(), settings_.port,
                                       timeout);
    if (context_)
        redisSetTimeout(context_, timeout);
    if (!context_ || context_->err) {
        error = context_ ? context_->errstr : "redis context allocation failed";
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
        return false;
    }

    if (!settings_.password.empty()) {
        redisReply *reply = static_cast<redisReply *>(
            redisCommand(context_, "AUTH %s", settings_.password.c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            error = reply && reply->str ? reply->str : "redis AUTH failed";
            if (reply)
                freeReplyObject(reply);
            redisFree(context_);
            context_ = nullptr;
            return false;
        }
        freeReplyObject(reply);
    }

    redisReply *reply =
        static_cast<redisReply *>(redisCommand(context_, "PING"));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        error = reply && reply->str ? reply->str : "redis PING failed";
        if (reply)
            freeReplyObject(reply);
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
    const std::string &account, bitevideo::UserProfile &profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string reconnectError;
    if (settings_.enabled && (!context_ || context_->err))
        connectUnlocked(reconnectError);
    if (!enabled())
        return false;

    const std::string key = profileCacheKey(account);
    redisReply *reply = static_cast<redisReply *>(
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
    if (!json || !json->isObject() || !(*json)["account"].isString() ||
        !(*json)["userName"].isString() || !(*json)["description"].isString() ||
        !(*json)["avatarPath"].isString() ||
        (*json)["account"].asString() != account) {
        redisReply *deleteReply = static_cast<redisReply *>(
            redisCommand(context_, "DEL %b", key.data(), key.size()));
        if (deleteReply)
            freeReplyObject(deleteReply);
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
    const bitevideo::UserProfile &profile, const std::string &version) {
    if (version.empty())
        return;
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
    std::string reconnectError;
    if (settings_.enabled && (!context_ || context_->err))
        connectUnlocked(reconnectError);
    if (!enabled())
        return;
    const std::string key = profileCacheKey(profile.account);
    const int baseTtl = settings_.profileCacheTtlSeconds;
    const int ttl = baseTtl + static_cast<int>(biteutil::Random::number(
                                  0, static_cast<std::size_t>(baseTtl)));
    const auto versionKey = key + ":generation";
    const std::string script =
        "if redis.call('GET',KEYS[2]) == ARGV[1] then return "
        "redis.call('SETEX',KEYS[1],ARGV[2],ARGV[3]) else return 0 end";
    const auto &versionString = version;
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        context_, "EVAL %b 2 %b %b %b %d %b", script.data(), script.size(),
        key.data(), key.size(), versionKey.data(), versionKey.size(),
        versionString.data(), versionString.size(), ttl, payload->data(),
        payload->size()));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        const std::string detail =
            reply && reply->str ? reply->str : "no reply";
        if (reply)
            freeReplyObject(reply);
        warnCacheFailure("SETEX", detail);
        return;
    }
    freeReplyObject(reply);
}

std::string RedisCachedUserRepository::generation(const std::string &account) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string error, nonce;
    if (settings_.enabled && (!context_ || context_->err))
        connectUnlocked(error);
    if (!enabled() || !bitesession::generateSessionToken(nonce, error))
        return "";
    const auto key = profileCacheKey(account) + ":generation";
    // 随机代次避免 Redis 重启/淘汰 generation 后出现从 0 开始的 ABA 回填。
    const std::string script =
        "local v=redis.call('GET',KEYS[1]); if not v then "
        "redis.call('SET',KEYS[1],ARGV[1]); return ARGV[1] end; return v";
    auto *reply = static_cast<redisReply *>(
        redisCommand(context_, "EVAL %b 1 %b %b", script.data(), script.size(),
                     key.data(), key.size(), nonce.data(), nonce.size()));
    if (!reply)
        return "";
    const auto version = reply->type == REDIS_REPLY_STRING
                             ? std::string(reply->str, reply->len)
                             : "";
    freeReplyObject(reply);
    return version;
}
bool RedisCachedUserRepository::invalidateProfile(const std::string &account) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!settings_.enabled)
        return true;
    std::string error;
    if (!context_ || context_->err)
        connectUnlocked(error);
    if (!enabled())
        return false;
    const auto key = profileCacheKey(account), versionKey = key + ":generation";
    std::string nonce;
    if (!bitesession::generateSessionToken(nonce, error))
        return false;
    const std::string script =
        "redis.call('SET',KEYS[2],ARGV[1]); return redis.call('DEL',KEYS[1])";
    auto *reply = static_cast<redisReply *>(
        redisCommand(context_, "EVAL %b 2 %b %b %b", script.data(),
                     script.size(), key.data(), key.size(), versionKey.data(),
                     versionKey.size(), nonce.data(), nonce.size()));
    const bool ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply)
        freeReplyObject(reply);
    return ok;
}

bool RedisCachedUserRepository::userProfile(
    const std::string &account, std::optional<bitevideo::UserProfile> &profile,
    std::string &error) {
    bitevideo::UserProfile cached;
    if (loadCachedProfile(account, cached)) {
        profile = std::move(cached);
        error.clear();
        return true;
    }
    const auto version = generation(account);
    if (!inner_.userProfile(account, profile, error))
        return false;
    if (profile)
        storeCachedProfile(*profile, version);
    return true;
}

bool RedisCachedUserRepository::updateUserProfile(
    const std::string &account, const std::string &userName,
    const std::string &description,
    std::optional<bitevideo::UserProfile> &profile, std::string &error) {
    const bool succeeded = inner_.updateUserProfile(
        account, userName, description, profile, error);
    if (succeeded)
        invalidateProfile(account);
    return succeeded;
}

bool RedisCachedUserRepository::updateAvatarPath(const std::string &account,
                                                 const std::string &avatarPath,
                                                 bool &updated,
                                                 std::string &error) {
    const bool succeeded =
        inner_.updateAvatarPath(account, avatarPath, updated, error);
    if (succeeded && updated)
        invalidateProfile(account);
    return succeeded;
}

bool RedisCachedUserRepository::passwordLogin(
    const std::string &account, const std::string &password,
    std::optional<bitevideo::UserProfile> &profile, std::string &error) {
    const bool succeeded =
        inner_.passwordLogin(account, password, profile, error);
    // 登录以 MySQL 为准；不从可能过期的登录快照填充资料缓存。
    return succeeded;
}

bool RedisCachedUserRepository::createEmailCode(
    const std::string &email, bitevideo::EmailCodeSession &session,
    std::string &error) {
    return inner_.createEmailCode(email, session, error);
}

bool RedisCachedUserRepository::emailLogin(
    const std::string &email, const std::string &authcodeId,
    const std::string &authcode, std::optional<bitevideo::UserProfile> &profile,
    std::string &error) {
    const bool succeeded =
        inner_.emailLogin(email, authcodeId, authcode, profile, error);
    // 登录以 MySQL 为准；不从可能过期的登录快照填充资料缓存。
    return succeeded;
}

bool RedisCachedUserRepository::logout(const std::string &account,
                                       bool &knownUser, std::string &error) {
    return inner_.logout(account, knownUser, error);
}

} // namespace svc_user
