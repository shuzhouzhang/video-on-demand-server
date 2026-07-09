#pragma once

#include <string>

namespace svc_transcode {

class RpcFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_transcode
