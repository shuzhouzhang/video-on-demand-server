#include "config.h"
#include "http_server.h"
#include "../database/database.h"
#include "../repository/admin_repository.h"
#include "../svc_user/source/user_repository.h"
#include "../svc_video/source/video_repository.h"

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

    bitedb::Database database;
    if (!database.connect(settings->database, error)) {
        ERR("{}", error);
        return 1;
    }
    INF("{}", "MySQL connection is healthy");
    INF("HTTP server listening on 0.0.0.0:{}", settings->server.port);

    bitevideo::MySqlVideoRepository videos(database);
    biteuser::MySqlUserRepository users(database);
    biterepo::MySqlAdminRepository admins(database);
    const biterepo::RepositorySet repositories{
        &users, &videos, &videos, &admins};
    biteserver::HttpServer server(
        repositories, biteserver::ServiceRole::All, "video_server", nullptr,
        settings->auth.enforceGatewayIdentity);
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("HTTP server failed to listen on port {}", settings->server.port);
        return 1;
    }
    return 0;
}
