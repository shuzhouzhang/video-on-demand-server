#include "svc_server.h"

#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/http_server.h"
#include "../../common/redis_session_manager.h"
#include "../../database/database.h"
#include "user_repository.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace svc_user {

UserServerBuilder& UserServerBuilder::withConfigPath(std::string configPath) {
    configPath_ = std::move(configPath);
    return *this;
}

int UserServerBuilder::start() const {
    std::string error;
    const auto settings = biteconfig::Config::load(configPath_, error);
    if (!settings) {
        std::cerr << "user_service 启动失败: " << error << '\n';
        return 1;
    }

    bitelog::bitelog_init(settings->log);

    bitedb::Database database;
    if (!database.connect(settings->database, error)) {
        ERR("{}", error);
        return 1;
    }
    INF("{} MySQL connection is healthy", "user_service");
    INF("user_service listening on 0.0.0.0:{}", settings->server.port);

    bitesession::RedisSessionManager sessionManager(settings->redis);
    if (!sessionManager.connect(error)) {
        ERR("user_service Redis connection failed: {}", error);
        return 1;
    }

    std::unique_ptr<bitevideo::VideoStore> repository =
        std::make_unique<biteuser::MySqlUserRepository>(database);
    biteserver::HttpServer server(*repository,
                                  biteserver::ServiceRole::User,
                                  "user_service",
                                  sessionManager.enabled()
                                      ? &sessionManager : nullptr);
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("user_service failed to listen on port {}", settings->server.port);
        return 1;
    }
    return 0;
}

}  // namespace svc_user
