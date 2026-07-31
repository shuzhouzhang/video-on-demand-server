#include "brpc_http_bridge.h"

#ifdef VOD_ENABLE_REFERENCE_RUNTIME

#include "runtime.pb.h"

#include <brpc/channel.h>
#include <brpc/closure_guard.h>
#include <brpc/controller.h>
#include <brpc/server.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

namespace biterpc {
namespace {

struct ParsedUrl {
    std::string host;
    int port = 80;
};

std::optional<ParsedUrl> parseHttpUrl(const std::string& url) {
    constexpr char prefix[] = "http://";
    if (url.rfind(prefix, 0) != 0) return std::nullopt;
    std::string hostPort = url.substr(sizeof(prefix) - 1);
    const std::size_t slash = hostPort.find('/');
    if (slash != std::string::npos) hostPort.resize(slash);
    const std::size_t colon = hostPort.rfind(':');
    ParsedUrl parsed;
    try {
        if (colon == std::string::npos) {
            parsed.host = hostPort;
        } else {
            parsed.host = hostPort.substr(0, colon);
            parsed.port = std::stoi(hostPort.substr(colon + 1));
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (parsed.host.empty() || parsed.port < 1 || parsed.port > 65535) {
        return std::nullopt;
    }
    return parsed;
}

std::string brpcAddress(std::string endpoint) {
    constexpr char httpPrefix[] = "http://";
    if (endpoint.rfind(httpPrefix, 0) == 0) {
        endpoint.erase(0, sizeof(httpPrefix) - 1);
    }
    const std::size_t slash = endpoint.find('/');
    if (slash != std::string::npos) endpoint.resize(slash);
    return endpoint;
}

bool useAttachment(const std::string& contentType) {
    return contentType.rfind("application/json", 0) != 0 &&
           contentType.rfind("text/", 0) != 0;
}

class InternalHttpService final
    : public vod::runtime::InternalHttpService {
public:
    InternalHttpService(std::string httpBaseUrl, int timeoutMs)
        : httpBaseUrl_(std::move(httpBaseUrl)), timeoutMs_(timeoutMs) {}

    void Forward(google::protobuf::RpcController* controller,
                 const vod::runtime::InternalHttpRequest* request,
                 vod::runtime::InternalHttpResponse* response,
                 google::protobuf::Closure* done) override {
        brpc::ClosureGuard doneGuard(done);
        auto* brpcController = static_cast<brpc::Controller*>(controller);
        const auto parsed = parseHttpUrl(httpBaseUrl_);
        if (!parsed) {
            response->set_error_code(500);
            response->set_error_message("invalid local HTTP bridge endpoint");
            return;
        }

        httplib::Headers headers;
        for (const auto& header : request->headers()) {
            headers.emplace(header.first, header.second);
        }
        headers.erase("Host");
        headers.erase("Content-Length");
        headers.erase("Transfer-Encoding");
        headers.emplace("X-Request-Id", request->request_id());
        if (!request->authenticated_account().empty()) {
            headers.erase("X-Authenticated-Account");
            headers.emplace("X-Authenticated-Account",
                            request->authenticated_account());
        }

        std::string body = request->body();
        if (body.empty() && !brpcController->request_attachment().empty()) {
            body = brpcController->request_attachment().to_string();
        }
        const auto contentTypeHeader = request->headers().find("Content-Type");
        const std::string contentType =
            contentTypeHeader == request->headers().end()
                ? "application/octet-stream"
                : contentTypeHeader->second;

        httplib::Client client(parsed->host, parsed->port);
        const std::chrono::milliseconds timeout(timeoutMs_);
        client.set_connection_timeout(timeout);
        client.set_read_timeout(timeout);
        client.set_write_timeout(timeout);

        if (request->method() != "GET" && request->method() != "POST") {
            response->set_error_code(405);
            response->set_error_message("unsupported internal HTTP method");
            return;
        }
        httplib::Result result = request->method() == "GET"
            ? client.Get(request->target().c_str(), headers)
            : client.Post(request->target().c_str(), headers, body,
                          contentType.c_str());
        if (!result) {
            response->set_error_code(503);
            response->set_error_message(httplib::to_string(result.error()));
            return;
        }

        response->set_status(result->status);
        response->set_request_id(request->request_id());
        response->set_content_type(result->get_header_value("Content-Type"));
        for (const auto& header : result->headers) {
            (*response->mutable_headers())[header.first] = header.second;
        }
        if (useAttachment(response->content_type())) {
            brpcController->response_attachment().append(result->body);
        } else {
            response->set_body(result->body);
        }
    }

private:
    std::string httpBaseUrl_;
    int timeoutMs_;
};

}  // namespace

struct BrpcHttpBridge::Impl {
    Impl(std::string httpBaseUrl, int timeoutMs)
        : service(std::move(httpBaseUrl), timeoutMs) {}

    InternalHttpService service;
    brpc::Server server;
    bool started = false;
};

BrpcHttpBridge::BrpcHttpBridge(std::string httpBaseUrl, int timeoutMs)
    : impl_(std::make_unique<Impl>(std::move(httpBaseUrl), timeoutMs)) {}

BrpcHttpBridge::~BrpcHttpBridge() {
    if (impl_ && impl_->started) {
        impl_->server.Stop(0);
        impl_->server.Join();
    }
}

bool BrpcHttpBridge::start(const std::string& bindHost, int port,
                           std::string& error) {
    if (port < 1 || port > 65535) {
        error = "rpc.port must be configured when RPC is enabled";
        return false;
    }
    if (impl_->server.AddService(&impl_->service,
                                 brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        error = "failed to add internal brpc service";
        return false;
    }
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    const std::string address = bindHost + ":" + std::to_string(port);
    if (impl_->server.Start(address.c_str(), &options) != 0) {
        error = "failed to start internal brpc server at " + address;
        return false;
    }
    impl_->started = true;
    return true;
}

bool forwardOverBrpc(
    const std::string& endpoint,
    int timeoutMs,
    const httplib::Request& request,
    const std::string& target,
    const httplib::Headers& headers,
    const std::optional<std::string>& authenticatedAccount,
    const std::string& requestId,
    ForwardResponse& response,
    std::string& error) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = timeoutMs;
    options.connect_timeout_ms = timeoutMs;
    options.max_retry = 0;
    const std::string address = brpcAddress(endpoint);
    if (address.empty() || channel.Init(address.c_str(), &options) != 0) {
        error = "cannot initialize brpc channel for " + address;
        return false;
    }

    vod::runtime::InternalHttpService_Stub stub(&channel);
    vod::runtime::InternalHttpRequest rpcRequest;
    vod::runtime::InternalHttpResponse rpcResponse;
    rpcRequest.set_request_id(requestId);
    rpcRequest.set_method(request.method);
    rpcRequest.set_target(target);
    if (authenticatedAccount) {
        rpcRequest.set_authenticated_account(*authenticatedAccount);
    }
    for (const auto& header : headers) {
        (*rpcRequest.mutable_headers())[header.first] = header.second;
    }

    brpc::Controller controller;
    if (request.path == "/files/upload") {
        controller.request_attachment().append(request.body);
    } else {
        rpcRequest.set_body(request.body);
    }
    stub.Forward(&controller, &rpcRequest, &rpcResponse, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (rpcResponse.error_code() != 0) {
        error = rpcResponse.error_message();
        return false;
    }

    response.status = rpcResponse.status();
    response.contentType = rpcResponse.content_type();
    response.requestId = rpcResponse.request_id();
    for (const auto& header : rpcResponse.headers()) {
        response.headers.emplace(header.first, header.second);
    }
    response.body = rpcResponse.body();
    if (response.body.empty() && !controller.response_attachment().empty()) {
        response.body = controller.response_attachment().to_string();
    }
    return true;
}

}  // namespace biterpc

#endif
