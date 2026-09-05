#include "svc_rpc.h"
#include "video_routes.h"
#include "interaction_routes.h"


#include "../../common/bitelog.h"

namespace svc_video {

VideoRpcService::VideoRpcService(
    biterepo::IVideoRepository& videoRepository,
    biterepo::IInteractionRepository& interactionRepository,
    biterepo::IAdminRepository& adminRepository,
    bitesession::RedisSessionManager* sessions,
    bool enforceGatewayIdentity)
    : videoRepository_(videoRepository),
      interactionRepository_(interactionRepository),
      adminRepository_(adminRepository),
      sessions_(sessions),
      enforceGatewayIdentity_(enforceGatewayIdentity) {}

int VideoRpcService::listen(const std::string& host, std::uint16_t port) {
    biterepo::RepositorySet repositories;
    repositories.videos = &videoRepository_;
    repositories.interactions = &interactionRepository_;
    repositories.admins = &adminRepository_;
    const biteserver::RouteContext context{repositories, sessions_, enforceGatewayIdentity_};
    biteserver::HttpServer server("video_service", [context](httplib::Server& http) {
        biteserver::registerVideoRoutes(http, context);
        biteserver::registerInteractionRoutes(http, context);
        biteserver::registerSmokeRoutes(http, context);
    });
    if (!server.listen(host, port)) {
        ERR("video_service failed to listen on port {}", port);
        return 1;
    }
    return 0;
}

}  // namespace svc_video
