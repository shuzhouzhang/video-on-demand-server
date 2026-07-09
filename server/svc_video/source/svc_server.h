#pragma once

#include <string>

namespace svc_video {

class VideoServerBuilder {
public:
    VideoServerBuilder& withConfigPath(std::string configPath);
    int start() const;

private:
    std::string configPath_ = "conf/video_service.local.json";
};

}  // namespace svc_video
