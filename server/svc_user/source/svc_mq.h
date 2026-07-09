#pragma once

#include <string>

namespace svc_user {

class MessageQueueFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_user
