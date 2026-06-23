#include "../../source/http_server.h"
#include "../../source/util.h"

#include <iostream>
#include <string>
#include <thread>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    std::cout << "[PASS] " << message << '\n';
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    biteserver::HttpServer server;
    const int port = server.bindToAnyPort("127.0.0.1");
    ok &= expect(port > 0, "bind an available local port");
    if (port <= 0) {
        return 1;
    }

    std::thread serverThread([&server]() { server.listenAfterBind(); });

    httplib::Client client("127.0.0.1", port);
    const auto health = client.Get("/health");
    ok &= expect(health && health->status == 200,
                 "GET /health returns HTTP 200");
    ok &= expect(health &&
                     health->get_header_value("Content-Type").find(
                         "application/json") != std::string::npos,
                 "GET /health returns JSON");

    if (health) {
        const auto body = biteutil::JSON::unserialize(health->body);
        ok &= expect(body && (*body)["code"].asInt() == 0 &&
                         (*body)["data"]["status"].asString() == "UP",
                     "GET /health returns healthy status");
    }

    const auto missing = client.Get("/missing");
    ok &= expect(missing && missing->status == 404,
                 "unknown route returns HTTP 404");

    server.stop();
    serverThread.join();
    return ok ? 0 : 1;
}
