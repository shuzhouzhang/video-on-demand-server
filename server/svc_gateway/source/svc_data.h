#pragma once

#include <string>

namespace svc_gateway {

class DataFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_gateway
