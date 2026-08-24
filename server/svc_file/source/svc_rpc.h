#pragma once

#include <string>

namespace svc_file {

class RpcFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_file
