#include "user_repository.h"

namespace biteuser {

MySqlUserRepository::MySqlUserRepository(bitedb::Database& database)
    : bitevideo::MySqlVideoRepository(database) {
}

}  // namespace biteuser
