#include "svc_server.h"
#include "svc_data.h"
#include "svc_rpc.h"
#include "svc_sync.h"

#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/redis_session_manager.h"
#include "../../database/database.h"
#include "../../repository/admin_repository.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace svc_video {

VideoServerBuilder& VideoServerBuilder::withConfigPath(std::string configPath) {
    configPath_ = std::move(configPath);
    return *this;
}

int VideoServerBuilder::start() const {
    std::string error;
    const auto settings = biteconfig::Config::load(configPath_, error);
    if (!settings) {
        std::cerr << "video_service 启动失败: " << error << '\n';
        return 1;
    }

    bitelog::bitelog_init(settings->log);

    bitedb::Database database;
    if (!database.connect(settings->database, error)) {
        ERR("{}", error);
        return 1;
    }
    INF("{} MySQL connection is healthy", "video_service");
    INF("video_service listening on 0.0.0.0:{}", settings->server.port);

    bitesession::RedisSessionManager sessionManager(settings->redis);
    if (!sessionManager.connect(error)) {
        ERR("video_service Redis connection failed: {}", error);
        return 1;
    }

    CacheDelete cacheDelete;
    CacheToDB cacheToDb(cacheDelete);
    (void)cacheToDb;

    VideoDataFacade data(database);
    auto repository = data.createRepository();
    biterepo::MySqlAdminRepository adminRepository(database);
    VideoRpcService rpc(*repository, *repository, adminRepository,
                        sessionManager.enabled() ? &sessionManager : nullptr,
                        settings->auth.enforceGatewayIdentity);
    return rpc.listen("0.0.0.0", settings->server.port);
}

}  // namespace svc_video
