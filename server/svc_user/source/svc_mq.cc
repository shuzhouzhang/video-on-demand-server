#include "svc_mq.h"

namespace svc_user {

const char* MessageQueueFacade::name() const noexcept {
    return "message queue boundary";
}

}  // namespace svc_user
