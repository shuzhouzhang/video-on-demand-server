#include "svc_rpc.h"

namespace svc_user {

const char* RpcFacade::name() const noexcept {
    return "RPC adapter boundary";
}

}  // namespace svc_user
