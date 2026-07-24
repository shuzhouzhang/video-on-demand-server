#include "svc_rpc.h"

#include "../../common/bitelog.h"

namespace svc_video {

VideoRpcService::VideoRpcService(
    biterepo::IVideoRepository& videoRepository,
    biterepo::IInteractionRepository& interactionRepository,
    biterepo::IAdminRepository& adminRepository,
    bitesession::RedisSessionManager* sessions)
    : videoRepository_(videoRepository),
      interactionRepository_(interactionRepository),
      adminRepository_(adminRepository),
      sessions_(sessions) {}

int VideoRpcService::listen(const std::string& host, std::uint16_t port) {
    biterepo::RepositorySet repositories;
    repositories.videos = &videoRepository_;
    repositories.interactions = &interactionRepository_;
    repositories.admins = &adminRepository_;
    biteserver::HttpServer server(repositories,
                                  biteserver::ServiceRole::Video,
                                  "video_service",
                                  sessions_);
    if (!server.listen(host, port)) {
        ERR("video_service failed to listen on port {}", port);
        return 1;
    }
    return 0;
}

}  // namespace svc_video
