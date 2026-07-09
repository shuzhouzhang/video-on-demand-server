#include "svc_data.h"

#include "user_repository.h"

namespace svc_user {

UserDataFacade::UserDataFacade(bitedb::Database& database) : database_(database) {}

std::unique_ptr<bitevideo::VideoStore> UserDataFacade::createRepository() const {
    return std::make_unique<biteuser::MySqlUserRepository>(database_);
}

}  // namespace svc_user
