#include "svc_data.h"

namespace svc_user {

const char* DataFacade::name() const noexcept {
    return "data access boundary";
}

}  // namespace svc_user
