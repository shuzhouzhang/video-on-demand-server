#pragma once

#include <string>

namespace svc_user {

class DataFacade {
public:
    const char* name() const noexcept;
};

}  // namespace svc_user
