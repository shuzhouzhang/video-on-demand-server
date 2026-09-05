#pragma once

#ifdef VOD_ENABLE_REFERENCE_RUNTIME

#include <httplib.h>

#include <memory>
#include <optional>
#include <string>

namespace google::protobuf { class Service; }

namespace biterpc {

struct ForwardResponse {
    int status = 0;
    httplib::Headers headers;
    std::string body;
    std::string contentType;
    std::string requestId;
};

class BrpcHttpBridge {
public:
    BrpcHttpBridge(std::string httpBaseUrl, int timeoutMs);
    ~BrpcHttpBridge();

    bool start(const std::string& bindHost, int port, std::string& error);
    // 服务对象由调用者持有，生命周期必须覆盖 bridge；只允许启动前注册。
    bool addService(google::protobuf::Service& service, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool forwardOverBrpc(const std::string& endpoint,
                     int timeoutMs,
                     const httplib::Request& request,
                     const std::string& target,
                     const httplib::Headers& headers,
                     const std::optional<std::string>& authenticatedAccount,
                     const std::string& requestId,
                     ForwardResponse& response,
                     std::string& error);

}  // namespace biterpc

#endif
