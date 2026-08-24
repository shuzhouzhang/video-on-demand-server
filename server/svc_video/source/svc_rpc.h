#pragma once

#include "../../common/http_server.h"
#include "../../common/redis_session_manager.h"

#include <cstdint>
#include <string>

namespace svc_video {

class VideoRpcService {
public:
    VideoRpcService(biterepo::IVideoRepository& videoRepository,
                    biterepo::IInteractionRepository& interactionRepository,
                    biterepo::IAdminRepository& adminRepository,
                    bitesession::RedisSessionManager* sessions,
                    bool enforceGatewayIdentity);

    int listen(const std::string& host, std::uint16_t port);

private:
    biterepo::IVideoRepository& videoRepository_;
    biterepo::IInteractionRepository& interactionRepository_;
    biterepo::IAdminRepository& adminRepository_;
    bitesession::RedisSessionManager* sessions_;
    bool enforceGatewayIdentity_;
};

}  // namespace svc_video
