#include "svc_data.h"

namespace svc_gateway {

const char* DataFacade::name() const noexcept {
    return "data access boundary";
}

}  // namespace svc_gateway
