#include "svc_rpc.h"

#include "../../common/bitelog.h"

namespace svc_user {

UserRpcService::UserRpcService(bitevideo::VideoStore& repository,
             bitesession::RedisSessionManager* sessions)
    : repository_(repository), sessions_(sessions) {}

int UserRpcService::listen(const std::string& host, std::uint16_t port) {
    biteserver::HttpServer server(repository_,
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
