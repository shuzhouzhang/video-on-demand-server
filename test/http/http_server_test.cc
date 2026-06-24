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

class FakeVideoStore : public bitevideo::VideoStore {
public:
    bool list(std::vector<bitevideo::Video>& videos,
              std::string& error) override {
        error.clear();
        videos = {{"video-001", "测试视频", "测试用户", "6-23", 558,
                   "36000", "256", "科技", {"编程开发", "软件工具"},
                   "HTTP测试数据"}};
        return true;
    }

    bool findById(const std::string& videoId,
                  std::optional<bitevideo::Video>& video,
                  std::string& error) override {
        error.clear();
        if (videoId == "video-001") {
            video = bitevideo::Video{
                "video-001", "测试视频", "测试用户", "6-23", 558,
                "36000", "256", "科技", {"编程开发", "软件工具"},
                "HTTP测试数据"};
        } else {
            video.reset();
        }
        return true;
    }
};

}  // namespace

int main() {
    bool ok = true;
    FakeVideoStore videoStore;
    biteserver::HttpServer server(videoStore);
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

    const auto videos = client.Get("/videos");
    ok &= expect(videos && videos->status == 200,
                 "GET /videos returns HTTP 200");
    if (videos) {
        const auto body = biteutil::JSON::unserialize(videos->body);
        ok &= expect(body && body->isArray() && body->size() == 1,
                     "GET /videos returns a JSON array");
        ok &= expect(body && (*body)[0]["id"].asString() == "video-001" &&
                         (*body)[0]["duration"].asString() == "09:18" &&
                         (*body)[0]["tags"].isArray(),
                     "GET /videos matches the client contract");
    }

    const auto detail = client.Get("/videos/detail?id=video-001");
    ok &= expect(detail && detail->status == 200,
                 "GET /videos/detail returns HTTP 200");
    if (detail) {
        const auto body = biteutil::JSON::unserialize(detail->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["video"]["id"].asString() == "video-001",
                     "GET /videos/detail returns the requested video");
    }

    const auto missingId = client.Get("/videos/detail");
    if (missingId) {
        const auto body = biteutil::JSON::unserialize(missingId->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "GET /videos/detail rejects a missing id");
    }

    const auto unknown = client.Get("/videos/detail?id=missing-video");
    if (unknown) {
        const auto body = biteutil::JSON::unserialize(unknown->body);
        ok &= expect(body && !(*body)["success"].asBool() &&
                         !(*body)["message"].asString().empty(),
                     "GET /videos/detail reports an unknown video");
    }

    server.stop();
    serverThread.join();
    return ok ? 0 : 1;
}
