#include "svc_rpc.h"

namespace svc_file {

const char* RpcFacade::name() const noexcept {
    return "RPC adapter boundary";
}

}  // namespace svc_file
