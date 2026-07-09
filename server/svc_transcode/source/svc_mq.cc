#include "svc_mq.h"

namespace svc_transcode {

MessageQueueFacade::MessageQueueFacade(SvcWorker& worker) : worker_(worker) {}

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
    job.note = "当前为第一阶段转码服务骨架，任务已进入本地 worker 队列，尚未接入 HLS/FFmpeg/MQ";

    return worker_.addTask([job]() {
        // 第一阶段只建立执行边界，后续在这里接入 FFmpeg/HLS 转码流程。
        (void)job;
    }, error);
}

}  // namespace svc_transcode
