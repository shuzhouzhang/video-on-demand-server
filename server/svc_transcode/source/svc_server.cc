#include "svc_server.h"
#include "svc_data.h"
#include "svc_worker.h"

#include "../../common/auth.h"
#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/util.h"
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
#include "../../common/brpc_http_bridge.h"
#include "../../common/etcd_registry.h"
#include "../../common/outbox.h"
#include "../../common/rabbitmq_consumer.h"
#include "../../common/rabbitmq_publisher.h"
#include "message.pb.h"
#endif
#include "../../database/database.h"

#include <httplib.h>
#include <jsoncpp/json/json.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

void setJsonResponse(httplib::Response& response,
                     int status,
                     const Json::Value& body) {
    response.status = status;
    response.set_content(
        biteutil::JSON::serialize(body).value_or(
            R"({"success":false,"message":"serialization error"})"),
        "application/json; charset=utf-8");
}

void setError(httplib::Response& response, int status,
              const std::string& message) {
    Json::Value body;
    body["success"] = false;
    body["message"] = message;
    setJsonResponse(response, status, body);
}

void writeJob(Json::Value& data, const svc_transcode::TranscodeJob& job) {
    data["jobId"] = job.jobId;
    data["videoId"] = job.videoId;
    data["status"] = job.status;
    data["attempts"] = job.attempts;
    data["maxAttempts"] = job.maxAttempts;
    data["error"] = job.errorMessage;
}

std::optional<std::string> authenticatedAccount(
    const httplib::Request& request,
    const std::string& claimed,
    bool strict,
    httplib::Response& response) {
    const auto identity =
        biteauth::bindAuthenticatedAccount(request, claimed, strict);
    if (identity.status == biteauth::IdentityStatus::Unauthenticated) {
        setError(response, 401, "authentication required");
        return std::nullopt;
    }
    if (identity.status == biteauth::IdentityStatus::Forbidden) {
        setError(response, 403, "account does not match authenticated user");
        return std::nullopt;
    }
    if (identity.account.empty()) {
        setError(response, 400, "account is required");
        return std::nullopt;
    }
    return identity.account;
}

void registerRoutes(httplib::Server& server,
                    svc_transcode::ITranscodeRepository& repository,
                    bool strictIdentity) {
    server.Get("/health", [](const httplib::Request&,
                             httplib::Response& response) {
        Json::Value body;
        body["code"] = 0;
        body["message"] = "ok";
        body["data"]["status"] = "UP";
        setJsonResponse(response, 200, body);
    });
    server.Get("/healthz", [](const httplib::Request&,
                              httplib::Response& response) {
        Json::Value body;
        body["success"] = true;
        body["service"] = "transcode_service";
        body["status"] = "ok";
        setJsonResponse(response, 200, body);
    });

    server.Post("/transcode/jobs", [&repository, strictIdentity](
                                      const httplib::Request& request,
                                      httplib::Response& response) {
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            setError(response, 400, "request body must be a JSON object");
            return;
        }
        const std::string videoId = (*payload)["videoId"].asString();
        if (videoId.empty()) {
            setError(response, 400, "videoId is required");
            return;
        }
        const auto account = authenticatedAccount(
            request, (*payload)["account"].asString(), strictIdentity,
            response);
        if (!account) return;

        svc_transcode::TranscodeJob job;
        std::string error;
        const std::string requestId = request.has_header("X-Request-Id")
            ? request.get_header_value("X-Request-Id") : "";
        if (!repository.enqueueForVideo(videoId, *account, requestId,
                                        job, error)) {
            setError(response, error.find("not found") != std::string::npos
                                   ? 404
                                   : 500,
                     error);
            return;
        }
        Json::Value body;
        body["success"] = true;
        body["message"] = "transcode job accepted";
        writeJob(body["data"], job);
        body["data"]["note"] = "job is persisted and processed asynchronously";
        setJsonResponse(response, 202, body);
    });

    server.Get("/transcode/jobs", [&repository, strictIdentity](
                                     const httplib::Request& request,
                                     httplib::Response& response) {
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string claimed = request.has_param("account")
            ? request.get_param_value("account") : "";
        if (videoId.empty()) {
            setError(response, 400, "videoId is required");
            return;
        }
        const auto account = authenticatedAccount(
            request, claimed, strictIdentity, response);
        if (!account) return;
        std::optional<svc_transcode::TranscodeJob> job;
        std::string error;
        if (!repository.findByVideoId(videoId, *account, job, error)) {
            setError(response, 500, error);
            return;
        }
        if (!job) {
            setError(response, 404, "transcode job not found");
            return;
        }
        Json::Value body;
        body["success"] = true;
        body["message"] = "ok";
        writeJob(body["data"], *job);
        setJsonResponse(response, 200, body);
    });

    server.Post("/transcode/jobs/retry", [&repository, strictIdentity](
                                            const httplib::Request& request,
                                            httplib::Response& response) {
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            setError(response, 400, "request body must be a JSON object");
            return;
        }
        const std::string videoId = (*payload)["videoId"].asString();
        if (videoId.empty()) {
            setError(response, 400, "videoId is required");
            return;
        }
        const auto account = authenticatedAccount(
            request, (*payload)["account"].asString(), strictIdentity,
            response);
        if (!account) return;
        bool updated = false;
        std::string error;
        if (!repository.retry(videoId, *account, updated, error)) {
            setError(response, 500, error);
            return;
        }
        if (!updated) {
            setError(response, 409, "only completed or failed jobs can be retried");
            return;
        }
        Json::Value body;
        body["success"] = true;
        body["message"] = "transcode job queued for retry";
        body["data"]["videoId"] = videoId;
        body["data"]["status"] = "PENDING";
        setJsonResponse(response, 202, body);
    });
}

}  // namespace

namespace svc_transcode {

TranscodeServerBuilder& TranscodeServerBuilder::withConfigPath(
    std::string configPath) {
    configPath_ = std::move(configPath);
    return *this;
}

int TranscodeServerBuilder::start() const {
    std::string error;
    const auto settings = biteconfig::Config::load(configPath_, error);
    if (!settings) {
        std::cerr << "transcode_service startup failed: " << error << std::endl;
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
            ERR("transcode_service brpc startup failed: {}", error);
            return 1;
        }
    }
    std::unique_ptr<bitesvc::EtcdServiceProvider> serviceProvider;
    if (settings->registry.enabled) {
        bitesvc::ServiceEndpoint endpoint{
            "transcode_service",
            "http://127.0.0.1:" + std::to_string(
                settings->rpc.enabled ? settings->rpc.port
                                      : settings->server.port),
            "",
            settings->rpc.enabled ? "brpc" : "http"};
        serviceProvider = std::make_unique<bitesvc::EtcdServiceProvider>(
            settings->registry, "transcode_service", std::move(endpoint));
        if (!serviceProvider->start(error)) {
            ERR("transcode_service etcd registration failed: {}", error);
            return 1;
        }
    }
#endif

    bitedb::Database database;
    if (!database.connect(settings->database, error)) {
        ERR("transcode_service database connection failed: {}", error);
        return 1;
    }
    biteevent::MySqlOutboxRepository* outbox = nullptr;
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    std::unique_ptr<biteevent::MySqlOutboxRepository> outboxRepository;
    std::unique_ptr<biteevent::RabbitMqPublisher> eventPublisher;
    std::unique_ptr<biteevent::OutboxDispatcher> outboxDispatcher;
    std::unique_ptr<biteevent::OutboxWorker> outboxWorker;
    if (settings->rabbitmq.enabled) {
        outboxRepository =
            std::make_unique<biteevent::MySqlOutboxRepository>(database);
        outbox = outboxRepository.get();
        eventPublisher =
            std::make_unique<biteevent::RabbitMqPublisher>(settings->rabbitmq);
        outboxDispatcher = std::make_unique<biteevent::OutboxDispatcher>(
            *outboxRepository, *eventPublisher, 3, 5, 10);
        outboxWorker =
            std::make_unique<biteevent::OutboxWorker>(*outboxDispatcher, 500);
        outboxWorker->start();
    }
#endif
    MySqlTranscodeRepository repository(
        database, static_cast<unsigned int>(settings->transcode.maxAttempts),
        outbox);
    if (!repository.recoverExpired(error)) {
        ERR("transcode_service lease recovery failed: {}", error);
        return 1;
    }
    FfmpegRunner runner(settings->transcode);
    SvcWorker worker(repository, runner, settings->transcode);
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    std::unique_ptr<biteevent::ConsumedEventStore> consumedEvents;
    std::unique_ptr<biteevent::RabbitMqConsumer> transcodeConsumer;
    if (settings->transcode.enabled && settings->rabbitmq.enabled) {
        consumedEvents =
            std::make_unique<biteevent::ConsumedEventStore>(database);
        transcodeConsumer = std::make_unique<biteevent::RabbitMqConsumer>(
            settings->rabbitmq, "vod.transcode", "transcode.hls",
            "vod.transcode.hls",
            [&worker, store = consumedEvents.get()](
                const biteevent::ConsumedMessage& message,
                std::string& handlerError) {
                vod::api::EventEnvelope envelope;
                if (!envelope.ParseFromString(message.body) ||
                    envelope.kind() != vod::api::HLS_TRANSCODE_REQUESTED ||
                    envelope.event_id().empty()) {
                    handlerError = "invalid HLS transcode event envelope";
                    return false;
                }
                if (!message.eventId.empty() &&
                    message.eventId != envelope.event_id()) {
                    handlerError = "RabbitMQ message id does not match event envelope";
                    return false;
                }
                bool alreadyProcessed = false;
                if (!store->wasProcessed("transcode_service",
                                         envelope.event_id(),
                                         alreadyProcessed, handlerError)) {
                    return false;
                }
                if (alreadyProcessed) return true;

                bool processed = false;
                if (!worker.processOne(processed, handlerError)) return false;

                bool first = false;
                if (!store->markIfFirst("transcode_service",
                                        envelope.event_id(), first,
                                        handlerError)) {
                    return false;
                }
                return true;
            });
        transcodeConsumer->start();
    } else if (settings->transcode.enabled) {
        worker.start();
    }
#else
    if (settings->transcode.enabled) worker.start();
#endif

    httplib::Server server;
    registerRoutes(server, repository,
                   settings->auth.enforceGatewayIdentity);
    const char* httpHost = settings->rpc.enabled ? "127.0.0.1" : "0.0.0.0";
    INF("transcode_service listening on {}:{}", httpHost,
        settings->server.port);
    if (!server.listen(httpHost, settings->server.port)) {
        ERR("transcode_service failed to listen on port {}",
            settings->server.port);
        return 1;
    }
    return 0;
}

}  // namespace svc_transcode
