#pragma once

#include "../../database/database.h"
#include "../../repository/repository.h"

namespace biteuser {

class MySqlUserRepository final : public biterepo::IUserRepository {
public:
    explicit MySqlUserRepository(bitedb::Database& database);

    bool userProfile(
        const std::string& account,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) override;
    bool updateUserProfile(
        const std::string& account,
        const std::string& userName,
        const std::string& description,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) override;
    bool updateAvatarPath(const std::string& account,
                          const std::string& avatarPath,
                          bool& updated,
                          std::string& error) override;
    bool passwordLogin(
        const std::string& account,
        const std::string& password,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) override;
    bool createEmailCode(const std::string& email,
                         bitevideo::EmailCodeSession& session,
                         std::string& error) override;
    bool emailLogin(
        const std::string& email,
        const std::string& authcodeId,
        const std::string& authcode,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) override;
    bool logout(const std::string& account,
                bool& knownUser,
                std::string& error) override;

private:
    bitedb::Database& database_;
};

}  // namespace biteuser
