#pragma once

#include "../../database/database.h"
#include "../../repository/repository.h"
#include "user_repository.h"

#include <memory>

namespace svc_user {

class UserDataFacade {
  public:
    explicit UserDataFacade(bitedb::Database &database);

    std::unique_ptr<biteuser::MySqlUserRepository> createRepository() const;

  private:
    bitedb::Database &database_;
};

} // namespace svc_user
