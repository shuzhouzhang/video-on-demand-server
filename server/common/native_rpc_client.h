#pragma once
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
#include "brpc_http_bridge.h"
namespace biterpc {
bool hasNativeRoute(const httplib::Request& request);
// Gateway 将旧客户端 JSON 映射到强类型 RPC；已迁移路由出错时不偷偷回退。
bool forwardNative(const std::string& endpoint, int timeoutMs,
    const httplib::Request& request, const std::optional<std::string>& account,
    const std::string& requestId, ForwardResponse& response, std::string& error);
}
#endif
