#include "svc_rpc.h"

namespace svc_gateway {

const char* RpcFacade::name() const noexcept {
    return "RPC adapter boundary";
}

}  // namespace svc_gateway
