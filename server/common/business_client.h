#pragma once
#include "native_rpc_client.h"
namespace biterpc {
bool hasBusinessRoute(const httplib::Request &request);
bool forwardBusiness(const std::string &endpoint, int timeoutMs,
                     const httplib::Request &request,
                     const std::optional<std::string> &account,
                     const std::string &requestId, ForwardResponse &response,
                     std::string &error);
} // namespace biterpc
