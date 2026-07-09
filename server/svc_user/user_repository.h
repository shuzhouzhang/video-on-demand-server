#pragma once

#include "../database/database.h"
#include "../svc_video/video_repository.h"

namespace biteuser {

class UserRepository : public bitevideo::VideoStore {
};

class MySqlUserRepository final : public bitevideo::MySqlVideoRepository {
public:
    explicit MySqlUserRepository(bitedb::Database& database);
};

}  // namespace biteuser
