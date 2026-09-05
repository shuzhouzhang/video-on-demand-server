#pragma once

#include "../../common/config.h"
#include "../../repository/repository.h"

#include <mutex>

struct redisContext;

namespace svc_user {

// Cache-aside decorator for user profiles. Authentication and mutations stay
// authoritative in MySQL; Redis only accelerates profile reads.
class RedisCachedUserRepository final : public biterepo::IUserRepository {
  public:
    RedisCachedUserRepository(biterepo::IUserRepository &inner,
                              const biteconfig::RedisSettings &settings);
    ~RedisCachedUserRepository() override;

    bool connect(std::string &error);
    bool invalidateProfile(const std::string &account);

    bool userProfile(const std::string &account,
                     std::optional<bitevideo::UserProfile> &profile,
                     std::string &error) override;
    bool updateUserProfile(const std::string &account,
                           const std::string &userName,
                           const std::string &description,
                           std::optional<bitevideo::UserProfile> &profile,
                           std::string &error) override;
    bool updateAvatarPath(const std::string &account,
                          const std::string &avatarPath, bool &updated,
                          std::string &error) override;
    bool passwordLogin(const std::string &account, const std::string &password,
                       std::optional<bitevideo::UserProfile> &profile,
                       std::string &error) override;
    bool createEmailCode(const std::string &email,
                         bitevideo::EmailCodeSession &session,
                         std::string &error) override;
    bool emailLogin(const std::string &email, const std::string &authcodeId,
                    const std::string &authcode,
                    std::optional<bitevideo::UserProfile> &profile,
                    std::string &error) override;
    bool logout(const std::string &account, bool &knownUser,
                std::string &error) override;

  private:
    bool enabled() const;
    bool loadCachedProfile(const std::string &account,
                           bitevideo::UserProfile &profile);
    bool connectUnlocked(std::string &error);
    std::string generation(const std::string &account);
    void storeCachedProfile(const bitevideo::UserProfile &profile,
                            const std::string &generation);

    biterepo::IUserRepository &inner_;
    biteconfig::RedisSettings settings_;
    redisContext *context_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace svc_user
