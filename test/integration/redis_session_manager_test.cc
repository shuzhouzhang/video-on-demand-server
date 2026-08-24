#include "../../server/common/redis_session_manager.h"

#include <hiredis/hiredis.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string env(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    else std::cout << "[PASS] " << message << '\n';
    return condition;
}

}  // namespace

int main() {
    bool ok = true;
    biteconfig::RedisSettings settings;
    settings.enabled = true;
    settings.host = env("VIDEO_TEST_REDIS_HOST", "127.0.0.1");
    settings.port = static_cast<std::uint16_t>(
        std::stoi(env("VIDEO_TEST_REDIS_PORT", "6379")));
    settings.password = env("VIDEO_TEST_REDIS_PASSWORD", "");
    settings.sessionTtlSeconds = 120;

    bitesession::RedisSessionManager sessions(settings);
    std::string error;
    if (!expect(sessions.connect(error), "connect to integration Redis")) {
        std::cerr << error << '\n';
        return 1;
    }

    std::string aliceToken;
    std::string bobToken;
    ok &= expect(sessions.createToken("alice", aliceToken, error),
                 "create Alice token");
    ok &= expect(sessions.createToken("bob", bobToken, error),
                 "create Bob token");

    std::atomic<bool> concurrentOk{true};
    std::vector<std::thread> threads;
    for (int index = 0; index < 50; ++index) {
        threads.emplace_back([&, index]() {
            const bool alice = index % 2 == 0;
            const std::string& token = alice ? aliceToken : bobToken;
            const std::string expected = alice ? "alice" : "bob";
            for (int iteration = 0; iteration < 100; ++iteration) {
                std::string lookupError;
                const auto account =
                    sessions.accountForToken(token, lookupError);
                if (!lookupError.empty() || !account ||
                    *account != expected) {
                    concurrentOk.store(false);
                    return;
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();
    ok &= expect(concurrentOk.load(),
                 "50 threads never cross Redis token accounts");

    redisContext* killer = redisConnect(settings.host.c_str(), settings.port);
    if (killer && !killer->err) {
        if (!settings.password.empty()) {
            redisReply* auth = static_cast<redisReply*>(redisCommand(
                killer, "AUTH %s", settings.password.c_str()));
            if (auth) freeReplyObject(auth);
        }
        redisReply* killed = static_cast<redisReply*>(redisCommand(
            killer, "CLIENT KILL TYPE normal SKIPME yes"));
        if (killed) freeReplyObject(killed);
        redisFree(killer);
        const auto reconnected = sessions.accountForToken(bobToken, error);
        ok &= expect(reconnected && *reconnected == "bob" && error.empty(),
                     "broken hiredis connection is rebuilt and retried");
    } else {
        if (killer) redisFree(killer);
        ok &= expect(false, "open Redis control connection for reconnect test");
    }

    ok &= expect(sessions.deleteToken(aliceToken, error),
                 "delete Alice token");
    const auto deleted = sessions.accountForToken(aliceToken, error);
    ok &= expect(!deleted && error.empty(),
                 "missing token is distinct from Redis error");

    biteconfig::RedisSettings unavailable = settings;
    unavailable.port = 1;
    bitesession::RedisSessionManager broken(unavailable);
    const auto unavailableAccount = broken.accountForToken("synthetic", error);
    ok &= expect(!unavailableAccount && !error.empty(),
                 "Redis query failure reports dependency error");

    sessions.deleteToken(bobToken, error);
    return ok ? 0 : 1;
}
