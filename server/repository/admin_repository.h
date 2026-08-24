#pragma once

#include "repository.h"
#include "../database/database.h"

namespace biterepo {

class MySqlAdminRepository final : public IAdminRepository {
public:
    explicit MySqlAdminRepository(bitedb::Database& database);

    bool userAccess(
        const std::string& account,
        std::optional<bitevideo::UserAccess>& access,
        std::string& error) override;
    bool adminReviews(std::vector<bitevideo::AdminReview>& reviews,
                      std::string& error) override;
    bool updateReviewStatus(const std::string& videoId,
                            const std::string& status,
                            bool& updated,
                            std::string& error) override;
    bool adminUsers(std::vector<bitevideo::AdminUser>& users,
                    std::string& error) override;
    bool updateAdminUser(const std::string& account,
                         const std::string& action,
                         bool& updated,
                         std::string& error) override;
    bool smokeCleanup(const std::string& videoId,
                      const std::string& videoTitle,
                      const std::string& account,
                      const std::string& previousAvatarPath,
                      std::string& error) override;

private:
    bitedb::Database& database_;
};

}  // namespace biterepo
