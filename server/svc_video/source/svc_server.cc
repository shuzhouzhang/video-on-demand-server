#include "svc_server.h"
#include "svc_data.h"
#include "svc_rpc.h"
#include "svc_sync.h"

#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/elasticsearch.h"
#include "../../common/redis_session_manager.h"
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
#include "../../common/brpc_http_bridge.h"
#include "../../common/etcd_registry.h"
#endif
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

#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    std::unique_ptr<biterpc::BrpcHttpBridge> rpcBridge;
    if (settings->rpc.enabled) {
        rpcBridge = std::make_unique<biterpc::BrpcHttpBridge>(
            "http://127.0.0.1:" + std::to_string(settings->server.port),
            settings->rpc.timeoutMs);
        if (!rpcBridge->start(settings->rpc.bindHost, settings->rpc.port,
                              error)) {
            ERR("video_service brpc startup failed: {}", error);
            return 1;
        }
    }
    std::unique_ptr<bitesvc::EtcdServiceProvider> serviceProvider;
    if (settings->registry.enabled) {
        bitesvc::ServiceEndpoint endpoint{
            "video_service",
            "http://127.0.0.1:" + std::to_string(
                settings->rpc.enabled ? settings->rpc.port
                                      : settings->server.port),
            "",
            settings->rpc.enabled ? "brpc" : "http"};
        serviceProvider = std::make_unique<bitesvc::EtcdServiceProvider>(
            settings->registry, "video_service", std::move(endpoint));
        if (!serviceProvider->start(error)) {
            ERR("video_service etcd registration failed: {}", error);
            return 1;
        }
    }
#endif

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

    std::unique_ptr<bitesearch::IVideoSearchIndex> searchIndex;
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    if (settings->elasticsearch.enabled) {
        auto elasticsearch = std::make_unique<bitesearch::ElasticsearchVideoIndex>(
            settings->elasticsearch);
        if (!elasticsearch->ensureIndex(error)) {
            WRN("video_service Elasticsearch initialization deferred: {}", error);
        }
        searchIndex = std::move(elasticsearch);
    }
#endif
    VideoDataFacade data(database, searchIndex.get());
    auto repository = data.createRepository();
    biterepo::MySqlAdminRepository adminRepository(database);
    VideoRpcService rpc(*repository, *repository, adminRepository,
                   sessionManager.enabled() ? &sessionManager : nullptr,
                   settings->auth.enforceGatewayIdentity);
    return rpc.listen(settings->rpc.enabled ? "127.0.0.1" : "0.0.0.0",
                      settings->server.port);
}

}  // namespace svc_video
