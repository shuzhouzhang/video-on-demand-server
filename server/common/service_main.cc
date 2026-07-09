#include "config.h"
#include "http_server.h"
#include "redis_session_manager.h"
#include "../database/database.h"
#include "../svc_user/user_repository.h"
#include "../svc_video/video_repository.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

biteserver::ServiceRole roleFromProgramName(const std::string& programName,
                                            std::string& serviceName) {
#if defined(BITE_SERVICE_ROLE_USER)
    (void)programName;
    serviceName = "user_service";
    return biteserver::ServiceRole::User;
#elif defined(BITE_SERVICE_ROLE_VIDEO)
    (void)programName;
    serviceName = "video_service";
    return biteserver::ServiceRole::Video;
#elif defined(BITE_SERVICE_ROLE_FILE)
    (void)programName;
    serviceName = "file_service";
    return biteserver::ServiceRole::File;
#else
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
    if (name.find("file_service") != std::string::npos) {
        serviceName = "file_service";
        return biteserver::ServiceRole::File;
    }
    serviceName = "video_server";
    return biteserver::ServiceRole::All;
#endif
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

    bitesession::RedisSessionManager sessionManager(settings->redis);
    if (!sessionManager.connect(error)) {
        ERR("{} Redis connection failed: {}", serviceName, error);
        return 1;
    }

    std::unique_ptr<bitevideo::VideoStore> repository;
    if (role == biteserver::ServiceRole::User) {
        repository = std::make_unique<biteuser::MySqlUserRepository>(database);
    } else {
        repository = std::make_unique<bitevideo::MySqlVideoRepository>(database);
    }

    biteserver::HttpServer server(*repository, role, serviceName,
                                  sessionManager.enabled()
                                      ? &sessionManager : nullptr);
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("{} failed to listen on port {}", serviceName, settings->server.port);
        return 1;
    }
    return 0;
}
