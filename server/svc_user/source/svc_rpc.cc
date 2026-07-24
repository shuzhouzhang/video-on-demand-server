#include "svc_rpc.h"

#include "../../common/bitelog.h"

namespace svc_user {

UserRpcService::UserRpcService(
    biterepo::IUserRepository& userRepository,
    biterepo::IAdminRepository& adminRepository,
    bitesession::RedisSessionManager* sessions)
    : userRepository_(userRepository),
      adminRepository_(adminRepository),
      sessions_(sessions) {}

int UserRpcService::listen(const std::string& host, std::uint16_t port) {
    biterepo::RepositorySet repositories;
    repositories.users = &userRepository_;
    repositories.admins = &adminRepository_;
    biteserver::HttpServer server(repositories,
                                  biteserver::ServiceRole::User,
                                  "user_service",
                                  sessions_);
    if (!server.listen(host, port)) {
        ERR("user_service failed to listen on port {}", port);
        return 1;
    }
    return 0;
}

}  // namespace svc_user
