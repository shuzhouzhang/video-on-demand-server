#include "../http/fake_repositories.h"
#include "../../server/svc_user/source/cached_user_repository.h"
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <hiredis/hiredis.h>
class PausedRepository : public FakeRepositories {
  public:
    std::mutex mutex;
    std::condition_variable cv;
    bool pause = false, captured = false, released = false;
    std::string name = "old";
    bool userProfile(const std::string &account,
                     std::optional<bitevideo::UserProfile> &profile,
                     std::string &) override {
        std::unique_lock<std::mutex> lock(mutex);
        profile = bitevideo::UserProfile{account, name, "", ""};
        if (pause) {
            captured = true;
            cv.notify_all();
            cv.wait(lock, [&] { return released; });
        }
        return true;
    }
};
int main() {
    bool ok = true;
    auto check = [&](bool passed, const char *name) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
        ok &= passed;
    };
    biteconfig::RedisSettings settings;
    settings.enabled = true;
    if (const auto *port = std::getenv("VIDEO_TEST_REDIS_PORT"))
        settings.port = std::stoi(port);
    PausedRepository repository;
    svc_user::RedisCachedUserRepository reader(repository, settings),
        writer(repository, settings);
    std::string error;
    if (!reader.connect(error) || !writer.connect(error)) {
        std::cerr << error;
        return 1;
    }
    check(writer.invalidateProfile("cache-race"),
          "initial cache invalidation succeeds");
    repository.pause = true;
    std::optional<bitevideo::UserProfile> stale;
    std::thread pending(
        [&] { reader.userProfile("cache-race", stale, error); });
    {
        std::unique_lock<std::mutex> lock(repository.mutex);
        repository.cv.wait(lock, [&] { return repository.captured; });
        repository.name = "new";
    }
    check(writer.invalidateProfile("cache-race"),
          "second instance invalidates during an in-flight database read");
    {
        std::lock_guard<std::mutex> lock(repository.mutex);
        repository.released = true;
        repository.pause = false;
    }
    repository.cv.notify_all();
    pending.join();
    std::optional<bitevideo::UserProfile> fresh;
    check(writer.userProfile("cache-race", fresh, error) && fresh &&
              fresh->userName == "new",
          "stale database snapshot cannot refill cache after invalidation");
    {
        std::lock_guard<std::mutex> lock(repository.mutex);
        repository.name = "uncached";
    }
    fresh.reset();
    check(reader.userProfile("cache-race", fresh, error) && fresh &&
              fresh->userName == "new",
          "fresh profile is shared across cache instances");
    check(writer.invalidateProfile("cache-race") &&
              writer.invalidateProfile("cache-race"),
          "duplicate invalidation is harmless");
    fresh.reset();
    check(reader.userProfile("cache-race", fresh, error) && fresh &&
              fresh->userName == "uncached",
          "duplicate events do not resurrect an old profile");

    auto *redis = redisConnect(settings.host.c_str(), settings.port);
    if (!redis || redis->err)
        return 1;
    auto clearRestartKeys = [&] {
        auto *reply = static_cast<redisReply *>(redisCommand(
            redis, "DEL vod:cache:user-profile:cache-restart "
                   "vod:cache:user-profile:cache-restart:generation"));
        if (reply)
            freeReplyObject(reply);
    };
    clearRestartKeys();
    {
        std::lock_guard<std::mutex> lock(repository.mutex);
        repository.name = "before-reset";
        repository.pause = true;
        repository.captured = false;
        repository.released = false;
    }
    std::thread beforeReset(
        [&] { reader.userProfile("cache-restart", stale, error); });
    {
        std::unique_lock<std::mutex> lock(repository.mutex);
        repository.cv.wait(lock, [&] { return repository.captured; });
        repository.name = "after-reset";
    }
    clearRestartKeys(); // 模拟 Redis 重启或 generation
                        // 被淘汰，而旧数据库读尚未返回。
    {
        std::lock_guard<std::mutex> lock(repository.mutex);
        repository.pause = false;
        repository.released = true;
    }
    repository.cv.notify_all();
    beforeReset.join();
    fresh.reset();
    check(writer.userProfile("cache-restart", fresh, error) && fresh &&
              fresh->userName == "after-reset",
          "generation loss cannot allow an old read to refill the cache");
    redisFree(redis);
    return ok ? 0 : 1;
}
