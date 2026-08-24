#pragma once

#include <string>

namespace svc_transcode {

class TranscodeServerBuilder {
public:
    TranscodeServerBuilder& withConfigPath(std::string configPath);
    int start() const;

private:
    std::string configPath_ = "conf/transcode_service.local.json";
};

}  // namespace svc_transcode
