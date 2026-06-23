#include "../../source/config.h"
#include "../../source/util.h"

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
        }
    })";
    ok &= expect(biteutil::FUTIL::write(filename, invalidPort),
                 "write invalid port fixture");
    ok &= expect(!biteconfig::Config::load(filename, error) &&
                     error.find("server.port") != std::string::npos,
                 "reject out-of-range port");

    std::remove(filename.c_str());
    std::remove(logFilename.c_str());
    ok &= expect(!biteconfig::Config::load(filename, error) && !error.empty(),
                 "report missing config file");

    return ok ? 0 : 1;
}
