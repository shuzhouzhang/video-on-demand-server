#include "redis_session_manager.h"

#include "auth.h"
#include "session_token.h"

#include <hiredis/hiredis.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace bitesession {
namespace {

struct RedisContextDeleter {
    void operator()(redisContext* context) const noexcept {
        if (context) redisFree(context);
    }
};

struct RedisReplyDeleter {
    void operator()(redisReply* reply) const noexcept {
        if (reply) freeReplyObject(reply);
    }
};

using RedisContextPtr =
    std::unique_ptr<redisContext, RedisContextDeleter>;
using RedisReplyPtr = std::unique_ptr<redisReply, RedisReplyDeleter>;

std::string connectionError(const redisContext* context,
                            const std::string& fallback) {
    if (context && context->errstr[0] != '\0') {
        return context->errstr;
    }
    return fallback;
}

RedisReplyPtr issueCommand(redisContext& context,
                           const std::vector<std::string>& arguments) {
    std::vector<const char*> values;
    std::vector<std::size_t> lengths;
    values.reserve(arguments.size());
    lengths.reserve(arguments.size());
    for (const auto& argument : arguments) {
        values.push_back(argument.data());
        lengths.push_back(argument.size());
    }
    return RedisReplyPtr(static_cast<redisReply*>(redisCommandArgv(
        &context, static_cast<int>(values.size()), values.data(),
        lengths.data())));
}

}  // namespace

struct RedisSessionManager::Impl {
    // hiredis synchronous contexts are not safe for concurrent commands. This
    // mutex intentionally serializes the single connection. It is a reliable
    // baseline but can become a throughput limit; replace it with a bounded
    // pool if Redis traffic grows.
    std::mutex mutex;
    RedisContextPtr context;

    bool connectLocked(const biteconfig::RedisSettings& settings,
                       std::string& error) {
        context.reset();
        RedisContextPtr candidate(
            redisConnect(settings.host.c_str(), settings.port));
        if (!candidate || candidate->err) {
            error = connectionError(candidate.get(),
                                    "Redis context allocation failed");
            return false;
        }

        if (!settings.password.empty()) {
            auto reply = issueCommand(*candidate, {"AUTH", settings.password});
            if (!reply || reply->type == REDIS_REPLY_ERROR) {
                error = reply ? "Redis authentication failed" :
                    connectionError(candidate.get(),
                                    "Redis authentication command failed");
                return false;
            }
        }

        auto ping = issueCommand(*candidate, {"PING"});
        if (!ping || ping->type == REDIS_REPLY_ERROR) {
            error = ping ? "Redis PING returned an error" :
                connectionError(candidate.get(), "Redis PING failed");
            return false;
        }
        context = std::move(candidate);
        error.clear();
        return true;
    }

    RedisReplyPtr commandLocked(
        const biteconfig::RedisSettings& settings,
        const std::vector<std::string>& arguments,
        std::string& error) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!context && !connectLocked(settings, error)) return {};

            auto reply = issueCommand(*context, arguments);
            if (reply) {
                if (reply->type == REDIS_REPLY_ERROR) {
                    error = "Redis command returned an error";
                    if (context->err) context.reset();
                    return {};
                }
                error.clear();
                return reply;
            }

            error = connectionError(context.get(), "Redis command failed");
            context.reset();
        }
        return {};
    }
};

RedisSessionManager::RedisSessionManager()
    : impl_(std::make_unique<Impl>()) {}

RedisSessionManager::RedisSessionManager(
    const biteconfig::RedisSettings& settings)
    : settings_(settings), impl_(std::make_unique<Impl>()) {}

RedisSessionManager::~RedisSessionManager() = default;

bool RedisSessionManager::connect(std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!settings_.enabled) {
        impl_->context.reset();
        return true;
    }
    return impl_->connectLocked(settings_, error);
}

bool RedisSessionManager::enabled() const {
    // This reports configured auth behavior, not transient connection health.
    // A broken Redis connection must never disable gateway authentication.
    return settings_.enabled;
}

bool RedisSessionManager::createToken(const std::string& account,
                                      std::string& token,
                                      std::string& error) {
    error.clear();
    if (!enabled()) {
        token.clear();
        return true;
    }
    if (!generateSessionToken(token, error)) return false;

    const std::string key = biteauth::redisSessionKeyForToken(token);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto reply = impl_->commandLocked(
        settings_, {"SETEX", key, std::to_string(settings_.sessionTtlSeconds),
                    account},
        error);
    if (!reply || reply->type != REDIS_REPLY_STATUS) {
        if (error.empty()) error = "Redis SETEX returned an invalid reply";
        token.clear();
        return false;
    }
    return true;
}

std::optional<std::string> RedisSessionManager::accountForToken(
    const std::string& token,
    std::string& error) {
    error.clear();
    if (!enabled() || token.empty()) return std::nullopt;

    const std::string key = biteauth::redisSessionKeyForToken(token);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto reply = impl_->commandLocked(settings_, {"GET", key}, error);
    if (!reply) return std::nullopt;
    if (reply->type == REDIS_REPLY_NIL) return std::nullopt;
    if (reply->type != REDIS_REPLY_STRING || !reply->str) {
        error = "Redis GET returned an invalid reply";
        return std::nullopt;
    }
    return std::string(reply->str, static_cast<std::size_t>(reply->len));
}

bool RedisSessionManager::deleteToken(const std::string& token,
                                      std::string& error) {
    error.clear();
    if (!enabled() || token.empty()) return true;

    const std::string key = biteauth::redisSessionKeyForToken(token);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto reply = impl_->commandLocked(settings_, {"DEL", key}, error);
    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        if (error.empty()) error = "Redis DEL returned an invalid reply";
        return false;
    }
    return true;
}

}  // namespace bitesession
