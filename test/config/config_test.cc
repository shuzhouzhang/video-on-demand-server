#include "../../source/config.h"
#include "../../source/util.h"
#include "../../server/common/service_registry.h"

#include <cstdio>
#include <iostream>
#include <string>

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
    const std::string filename = "/tmp/video_server_config_test.json";
    const std::string logFilename = "/tmp/video_server_config_test.log";
    std::string error;

    const std::string valid = R"({
        "server": {"port": 9000},
        "log": {
            "async": false,
            "level": 1,
            "pattern": "[%H:%M:%S] %v",
            "path": "/tmp/video_server_config_test.log"
        },
        "database": {
            "host": "127.0.0.1",
            "port": 3306,
            "user": "video_app",
            "password": "",
            "name": "video_on_demand"
        }
    })";
    ok &= expect(biteutil::FUTIL::write(filename, valid),
                 "write valid config fixture");
    const auto settings = biteconfig::Config::load(filename, error);
    ok &= expect(settings && settings->server.port == 9000,
                 "load server port");
    ok &= expect(settings && !settings->log.async &&
                     settings->log.path == logFilename,
                 "load log settings");
    ok &= expect(settings && settings->database.port == 3306 &&
                     settings->database.name == "video_on_demand",
                 "load database settings");
    if (settings) {
        bitelog::bitelog_init(settings->log);
        INF("{}", "configuration integration test");
        bitelog::g_logger->flush();
        std::string logBody;
        ok &= expect(biteutil::FUTIL::read(logFilename, logBody) &&
                         logBody.find("configuration integration test") !=
                             std::string::npos,
                     "apply configured log path");
    }

    ok &= expect(biteutil::FUTIL::write(filename, "{bad json"),
                 "write invalid JSON fixture");
    ok &= expect(!biteconfig::Config::load(filename, error) && !error.empty(),
                 "reject invalid JSON");

    const std::string invalidPort = R"({
        "server": {"port": 70000},
        "log": {
            "async": false,
            "level": 1,
            "pattern": "%v",
            "path": "stdout"
        },
        "database": {
            "host": "127.0.0.1",
            "port": 3306,
            "user": "video_app",
            "password": "",
            "name": "video_on_demand"
        }
    })";
    ok &= expect(biteutil::FUTIL::write(filename, invalidPort),
                 "write invalid port fixture");
    ok &= expect(!biteconfig::Config::load(filename, error) &&
                     error.find("server.port") != std::string::npos,
                 "reject out-of-range port");



    const std::string servicesFilename = "/tmp/video_services_config_test.json";
    const std::string servicesConfig = R"({
        "user_service": "http://127.0.0.1:10002",
        "video_service": "http://127.0.0.1:10003",
        "file_service": "http://127.0.0.1:10001",
        "transcode_service": "http://127.0.0.1:10004",
        "timeout_ms": 2500,
        "redis": {"enabled": true, "host": "127.0.0.1", "port": 6379}
    })";
    ok &= expect(biteutil::FUTIL::write(servicesFilename, servicesConfig),
                 "write service discovery fixture");
    bitesvc::DiscoverySettings discovery;
    ok &= expect(bitesvc::loadDiscoverySettings(servicesFilename, discovery, error),
                 "load service discovery settings");
    const auto* userService = discovery.registry.find("user_service");
    ok &= expect(userService && userService->baseUrl == "http://127.0.0.1:10002",
                 "find registered user service");
    ok &= expect(discovery.timeoutMs == 2500 && discovery.redis.enabled,
                 "load discovery timeout and redis settings");

    const std::string invalidServices = R"({"user_service":"127.0.0.1:10002"})";
    ok &= expect(biteutil::FUTIL::write(servicesFilename, invalidServices),
                 "write invalid service discovery fixture");
    ok &= expect(!bitesvc::loadDiscoverySettings(servicesFilename, discovery, error) &&
                     !error.empty(),
                 "reject invalid service discovery settings");

    std::remove(servicesFilename.c_str());
    std::remove(filename.c_str());
    std::remove(logFilename.c_str());
    ok &= expect(!biteconfig::Config::load(filename, error) && !error.empty(),
                 "report missing config file");

    return ok ? 0 : 1;
}
