#pragma once

#include <string>

namespace svc_user {

class RpcFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_user
