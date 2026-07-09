#pragma once

#include <string>

namespace svc_transcode {

struct TranscodeJob {
    std::string videoId;
    std::string filePath;
    std::string status;
    std::string note;
};

class MessageQueueFacade {
public:
    bool submitJob(const std::string& videoId,
                   const std::string& filePath,
                   TranscodeJob& job,
                   std::string& error) const;
};

}  // namespace svc_transcode
