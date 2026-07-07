#pragma once

#include <httplib.h>

#include <string>

namespace bitehttp {

struct DownstreamResponse {
    int status = 0;
    httplib::Headers headers;
    std::string body;
    std::string contentType;
};

class HttpClient {
public:
    HttpClient(std::string baseUrl, int timeoutMs);

    bool send(const httplib::Request& request,
              const std::string& target,
              const httplib::Headers& headers,
              DownstreamResponse& response,
              std::string& error) const;

    std::string endpoint(const std::string& target) const;

private:
    std::string baseUrl_;
    int timeoutMs_;
};

}  // namespace bitehttp
