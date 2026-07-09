#include "svc_mq.h"

namespace svc_transcode {

bool MessageQueueFacade::submitJob(const std::string& videoId,
                                   const std::string& filePath,
                                   TranscodeJob& job,
                                   std::string& error) const {
    if (videoId.empty() || filePath.empty()) {
        error = "videoId 和 filePath 不能为空";
        return false;
    }

    job.videoId = videoId;
    job.filePath = filePath;
    job.status = "PENDING";
    job.note = "当前为第一阶段转码服务骨架，尚未接入 HLS/FFmpeg/MQ";
    return true;
}

}  // namespace svc_transcode
