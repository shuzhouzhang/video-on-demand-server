#include "http_client.h"

#include <chrono>
#include <optional>
#include <string>

namespace bitehttp {
namespace {

struct ParsedUrl {
    std::string host;
    int port = 80;
};

std::optional<ParsedUrl> parseHttpUrl(const std::string& url) {
    constexpr char prefix[] = "http://";
    if (url.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    std::string hostPort = url.substr(sizeof(prefix) - 1);
    const std::size_t slash = hostPort.find('/');
    if (slash != std::string::npos) {
        hostPort = hostPort.substr(0, slash);
    }

    ParsedUrl parsed;
    const std::size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos) {
        parsed.host = hostPort;
        parsed.port = 80;
    } else {
        parsed.host = hostPort.substr(0, colon);
        try {
            parsed.port = std::stoi(hostPort.substr(colon + 1));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    if (parsed.host.empty() || parsed.port < 1 || parsed.port > 65535) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace

HttpClient::HttpClient(std::string baseUrl, int timeoutMs)
    : baseUrl_(std::move(baseUrl)),
      timeoutMs_(timeoutMs) {
}

bool HttpClient::send(const httplib::Request& request,
                      const std::string& target,
                      const httplib::Headers& headers,
                      DownstreamResponse& response,
                      std::string& error) const {
    const auto parsed = parseHttpUrl(baseUrl_);
    if (!parsed) {
        error = "invalid downstream url";
        return false;
    }

    httplib::Client client(parsed->host, parsed->port);
    const std::chrono::milliseconds timeout(timeoutMs_);
    client.set_connection_timeout(timeout);
    client.set_read_timeout(timeout);
    client.set_write_timeout(timeout);

    const std::string contentType = request.get_header_value("Content-Type");
    httplib::Result result = request.method == "GET"
        ? client.Get(target.c_str(), headers)
        : (request.is_multipart_form_data()
            ? [&]() {
                httplib::MultipartFormDataItems items;
                for (const auto& file : request.files) {
                    items.push_back(file.second);
                }
                return client.Post(target.c_str(), headers, items);
            }()
            : client.Post(target.c_str(), headers, request.body,
                          contentType.c_str()));

    if (!result) {
        error = httplib::to_string(result.error());
        return false;
    }

    response.status = result->status;
    response.headers = result->headers;
    response.body = result->body;
    response.contentType = result->get_header_value("Content-Type");
    return true;
}

std::string HttpClient::endpoint(const std::string& target) const {
    return baseUrl_ + target;
}

}  // namespace bitehttp
