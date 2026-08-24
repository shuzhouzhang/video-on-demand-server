#include "svc_mq.h"

namespace svc_video {

const char* MessageQueueFacade::name() const noexcept {
    return "message queue boundary";
}

}  // namespace svc_video
