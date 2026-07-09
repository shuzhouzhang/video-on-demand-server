#pragma once

#include "svc_worker.h"

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
    explicit MessageQueueFacade(SvcWorker& worker);

    bool submitJob(const std::string& videoId,
                   const std::string& filePath,
                   TranscodeJob& job,
                   std::string& error) const;

private:
    SvcWorker& worker_;
};

}  // namespace svc_transcode
