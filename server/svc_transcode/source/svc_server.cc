#include "svc_server.h"
#include "svc_mq.h"

#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/util.h"

#include <httplib.h>
#include <jsoncpp/json/json.h>

#include <iostream>
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

void registerRoutes(httplib::Server& server, const svc_transcode::MessageQueueFacade& mq) {
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
    server.Post("/transcode/jobs", [&mq](const httplib::Request& request,
                                      httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求体必须是 JSON 对象";
            setJsonResponse(response, 200, body);
            return;
        }
        const std::string videoId = (*payload)["videoId"].asString();
        const std::string filePath = (*payload)["filePath"].asString();
        svc_transcode::TranscodeJob job;
        std::string error;
        if (!mq.submitJob(videoId, filePath, job, error)) {
            body["success"] = false;
            body["message"] = error;
            setJsonResponse(response, 200, body);
            return;
        }
        body["success"] = true;
        body["message"] = "转码任务已接收";
        body["data"]["videoId"] = job.videoId;
        body["data"]["status"] = job.status;
        body["data"]["note"] = job.note;
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
        std::cerr << "transcode_service 启动失败: " << error << std::endl;
        return 1;
    }

    bitelog::bitelog_init(settings->log);

    httplib::Server server;
    MessageQueueFacade mq;
    registerRoutes(server, mq);

    INF("transcode_service listening on 0.0.0.0:{}", settings->server.port);
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("transcode_service failed to listen on port {}", settings->server.port);
        return 1;
    }
    return 0;
}

}  // namespace svc_transcode
