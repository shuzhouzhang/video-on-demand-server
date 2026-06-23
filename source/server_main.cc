#include "config.h"
#include "http_server.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    const std::string configPath = argc > 1 ? argv[1] : "conf/server.json";
    std::string error;
    const auto settings = biteconfig::Config::load(configPath, error);
    if (!settings) {
        std::cerr << "服务启动失败: " << error << '\n';
        return 1;
    }

    bitelog::bitelog_init(settings->log);
    INF("HTTP server listening on 0.0.0.0:{}", settings->server.port);

    biteserver::HttpServer server;
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("HTTP server failed to listen on port {}", settings->server.port);
        return 1;
    }
    return 0;
}
