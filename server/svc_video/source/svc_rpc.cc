#include "svc_rpc.h"

#include "../../common/bitelog.h"

namespace svc_video {

VideoRpcService::VideoRpcService(bitevideo::VideoStore& repository,
             bitesession::RedisSessionManager* sessions)
    : repository_(repository), sessions_(sessions) {}

int VideoRpcService::listen(const std::string& host, std::uint16_t port) {
    biteserver::HttpServer server(repository_,
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
