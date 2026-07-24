#pragma once

#include "../../common/http_server.h"
#include "../../common/redis_session_manager.h"

#include <cstdint>
#include <string>

namespace svc_user {

class UserRpcService {
public:
    UserRpcService(biterepo::IUserRepository& userRepository,
                   biterepo::IAdminRepository& adminRepository,
                   bitesession::RedisSessionManager* sessions);

    int listen(const std::string& host, std::uint16_t port);

private:
    biterepo::IUserRepository& userRepository_;
    biterepo::IAdminRepository& adminRepository_;
    bitesession::RedisSessionManager* sessions_;
};

}  // namespace svc_user
