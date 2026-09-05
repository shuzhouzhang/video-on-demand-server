#include "svc_rpc.h"
#include "user_routes.h"


#include "../../common/bitelog.h"

namespace svc_user {

UserRpcService::UserRpcService(
    biterepo::IUserRepository& userRepository,
    biterepo::IAdminRepository& adminRepository,
    bitesession::RedisSessionManager* sessions,
    bool enforceGatewayIdentity)
    : userRepository_(userRepository),
      adminRepository_(adminRepository),
      sessions_(sessions),
      enforceGatewayIdentity_(enforceGatewayIdentity) {}

int UserRpcService::listen(const std::string& host, std::uint16_t port) {
    biterepo::RepositorySet repositories;
    repositories.users = &userRepository_;
    repositories.admins = &adminRepository_;
    const biteserver::RouteContext context{repositories, sessions_, enforceGatewayIdentity_};
    biteserver::HttpServer server("user_service", [context](httplib::Server& http) {
        biteserver::registerUserRoutes(http, context);
        biteserver::registerSmokeRoutes(http, context);
    });
    if (!server.listen(host, port)) {
        ERR("user_service failed to listen on port {}", port);
        return 1;
    }
    return 0;
}

}  // namespace svc_user
