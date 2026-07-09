#pragma once

#include <string>

namespace svc_video {

class RpcFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_video
