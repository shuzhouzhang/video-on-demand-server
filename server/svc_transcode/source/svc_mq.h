#pragma once

#include <string>

namespace svc_transcode {

class MessageQueueFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_transcode
