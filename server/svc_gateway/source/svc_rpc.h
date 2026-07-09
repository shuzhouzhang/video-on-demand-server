#pragma once

#include <string>

namespace svc_gateway {

class RpcFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_gateway
