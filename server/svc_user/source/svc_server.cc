#include "svc_server.h"
#include "cached_user_repository.h"
#include "svc_data.h"
#include "svc_rpc.h"
#include "svc_sync.h"

#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/redis_session_manager.h"
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
#include "../../common/brpc_http_bridge.h"
#include "native_rpc.h"
#include "../../common/etcd_registry.h"
#endif
#include "../../database/database.h"
#include "../../repository/admin_repository.h"

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

    CacheDelete cacheDelete;
    (void)cacheDelete;

    UserDataFacade data(database);
    auto repository = data.createRepository();
    RedisCachedUserRepository cachedRepository(*repository, settings->redis);
    if (!cachedRepository.connect(error)) {
        ERR("user_service profile cache connection failed: {}", error);
        return 1;
    }
    biterepo::MySqlAdminRepository adminRepository(database);
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    const biteserver::RouteContext nativeContext{{&cachedRepository, nullptr, nullptr, &adminRepository},
        sessionManager.enabled() ? &sessionManager : nullptr, settings->auth.enforceGatewayIdentity};
    NativeUserService nativeService(nativeContext);
#endif
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    std::unique_ptr<biterpc::BrpcHttpBridge> rpcBridge;
    if (settings->rpc.enabled) {
        rpcBridge = std::make_unique<biterpc::BrpcHttpBridge>(
            "http://127.0.0.1:" + std::to_string(settings->server.port),
            settings->rpc.timeoutMs);
        if (!rpcBridge->addService(nativeService, error)) {
            ERR("native RPC registration failed: {}", error);
            return 1;
        }
        if (!rpcBridge->start(settings->rpc.bindHost, settings->rpc.port,
                              error)) {
            ERR("user_service brpc startup failed: {}", error);
            return 1;
        }
    }
    std::unique_ptr<bitesvc::EtcdServiceProvider> serviceProvider;
    if (settings->registry.enabled) {
        bitesvc::ServiceEndpoint endpoint{
            "user_service",
            "http://127.0.0.1:" + std::to_string(
                settings->rpc.enabled ? settings->rpc.port
                                      : settings->server.port),
            "",
            settings->rpc.enabled ? "brpc" : "http"};
        serviceProvider = std::make_unique<bitesvc::EtcdServiceProvider>(
            settings->registry, "user_service", std::move(endpoint));
        if (!serviceProvider->start(error)) {
            ERR("user_service etcd registration failed: {}", error);
            return 1;
        }
    }
#endif
    UserRpcService rpc(cachedRepository, adminRepository,
                   sessionManager.enabled() ? &sessionManager : nullptr,
                   settings->auth.enforceGatewayIdentity);
    return rpc.listen(settings->rpc.enabled ? "127.0.0.1" : "0.0.0.0",
                      settings->server.port);
}

}  // namespace svc_user
