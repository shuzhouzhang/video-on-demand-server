#include "config.h"
#include "database.h"
#include "http_server.h"
#include "video_repository.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

biteserver::ServiceRole roleFromProgramName(const std::string& programName,
                                            std::string& serviceName) {
    const std::string name = std::filesystem::path(programName).filename().string();
    if (name.find("user_service") != std::string::npos) {
        serviceName = "user_service";
        return biteserver::ServiceRole::User;
    }
    if (name.find("video_service") != std::string::npos) {
        serviceName = "video_service";
        return biteserver::ServiceRole::Video;
    }
    if (name.find("interaction_service") != std::string::npos) {
        serviceName = "interaction_service";
        return biteserver::ServiceRole::Interaction;
    }
    serviceName = "video_server";
    return biteserver::ServiceRole::All;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string serviceName;
    const biteserver::ServiceRole role = roleFromProgramName(argv[0], serviceName);
    const std::string configPath = argc > 1 ? argv[1] : "conf/server.json";

    std::string error;
    const auto settings = biteconfig::Config::load(configPath, error);
    if (!settings) {
        std::cerr << serviceName << " 启动失败: " << error << '\n';
        return 1;
    }

    bitelog::bitelog_init(settings->log);

    bitedb::Database database;
    if (!database.connect(settings->database, error)) {
        ERR("{}", error);
        return 1;
    }
    INF("{} MySQL connection is healthy", serviceName);
    INF("{} listening on 0.0.0.0:{}", serviceName, settings->server.port);

    bitevideo::MySqlVideoRepository videos(database);
    biteserver::HttpServer server(videos, role, serviceName);
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("{} failed to listen on port {}", serviceName, settings->server.port);
        return 1;
    }
    return 0;
}
