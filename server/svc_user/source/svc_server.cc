#include "svc_server.h"
#include "cached_user_repository.h"
#include "svc_data.h"
#include "svc_rpc.h"

#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/redis_session_manager.h"
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
#include "../../common/brpc_http_bridge.h"
#include "../../common/remote_object_storage.h"
#include "native_rpc.h"
#include "user_operations.h"
#include "../../common/outbox.h"
#include "../../common/rabbitmq_consumer.h"
#include "../../common/rabbitmq_publisher.h"
#include "message.pb.h"

#include "../../common/etcd_registry.h"
#endif
#include "../../database/database.h"
#include "../../repository/admin_repository.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace svc_user {

UserServerBuilder &UserServerBuilder::withConfigPath(std::string configPath) {
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

    UserDataFacade data(database);
    auto repository = data.createRepository();
    RedisCachedUserRepository cachedRepository(*repository, settings->redis);
    if (!cachedRepository.connect(error)) {
        ERR("user_service profile cache connection failed: {}", error);
        return 1;
    }
    biterepo::MySqlAdminRepository adminRepository(database);
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    biteevent::MySqlOutboxRepository profileOutbox(database);
    biteevent::RabbitMqPublisher profilePublisher(settings->rabbitmq);
    biteevent::OutboxDispatcher profileDispatcher(profileOutbox,
                                                  profilePublisher, 100, 5, 10);
    biteevent::OutboxWorker profileOutboxWorker(profileDispatcher, 500);
    biteevent::ConsumedEventStore profileEvents(database);
    std::unique_ptr<biteevent::RabbitMqConsumer> profileConsumer;
    if (settings->rabbitmq.enabled) {
        repository->enableEvents(&profileOutbox);
        profileConsumer = std::make_unique<biteevent::RabbitMqConsumer>(
            settings->rabbitmq, "vod.cache", "user.profile.invalidate",
            "vod.user.profile.invalidate",
            [&cachedRepository, &profileEvents](
                const biteevent::ConsumedMessage &message, std::string &error) {
                vod::api::EventEnvelope envelope;
                if (!envelope.ParseFromString(message.body) ||
                    envelope.kind() != vod::api::CACHE_INVALIDATION_REQUESTED ||
                    envelope.event_id().empty() ||
                    envelope.aggregate_id().empty()) {
                    error = "invalid profile cache event";
                    return false;
                }
                if (!message.eventId.empty() &&
                    message.eventId != envelope.event_id()) {
                    error = "cache event id mismatch";
                    return false;
                }
                bool processed = false;
                if (!profileEvents.wasProcessed("user_profile_cache",
                                                envelope.event_id(), processed,
                                                error)) {
                    error = "retryable: " + error;
                    return false;
                }
                if (processed)
                    return true;
                if (!cachedRepository.invalidateProfile(
                        envelope.aggregate_id())) {
                    error = "retryable: Redis cache invalidation unavailable";
                    return false;
                }
                bool first = false;
                const bool saved = profileEvents.markIfFirst(
                    "user_profile_cache", envelope.event_id(), first, error);
                if (!saved)
                    error = "retryable: " + error;
                return saved;
            });
        profileConsumer->start();
        profileOutboxWorker.start();
    }
#endif

#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    const biteserver::RouteContext nativeContext{
        {&cachedRepository, nullptr, nullptr, &adminRepository},
        sessionManager.enabled() ? &sessionManager : nullptr,
        settings->auth.enforceGatewayIdentity};
    bitestorage::RemoteObjectStorage mediaStorage(settings->registry,
                                                  settings->rpc.fileTimeoutMs);
    if (settings->registry.enabled && !mediaStorage.start(error)) {
        ERR("file discovery failed: {}", error);
        return 1;
    }
    NativeUserService nativeService(nativeContext);
    auto businessContext = nativeContext;
    if (settings->registry.enabled)
        businessContext.mediaStorage = &mediaStorage;
    UserOperations userOperations(businessContext);

#endif
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    std::unique_ptr<biterpc::BrpcHttpBridge> rpcBridge;
    if (settings->rpc.enabled) {
        rpcBridge = std::make_unique<biterpc::BrpcHttpBridge>(
            "http://127.0.0.1:" + std::to_string(settings->server.port),
            settings->rpc.timeoutMs);
        if (!rpcBridge->addService(userOperations, error)) {
            ERR("business RPC registration failed: {}", error);
            return 1;
        }
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
            "http://127.0.0.1:" + std::to_string(settings->rpc.enabled
                                                     ? settings->rpc.port
                                                     : settings->server.port),
            "", settings->rpc.enabled ? "brpc" : "http"};
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

} // namespace svc_user
