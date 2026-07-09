#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/util.h"

#include <httplib.h>
#include <jsoncpp/json/json.h>

#include <iostream>
#include <string>

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

}  // namespace

int main(int argc, char* argv[]) {
    const std::string configPath =
        argc > 1 ? argv[1] : "conf/transcode_service.local.json";

    std::string error;
    const auto settings = biteconfig::Config::load(configPath, error);
    if (!settings) {
        std::cerr << "transcode_service 启动失败: " << error << std::endl;
        return 1;
    }

    bitelog::bitelog_init(settings->log);

    httplib::Server server;
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
    server.Post("/transcode/jobs", [](const httplib::Request& request,
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
        if (videoId.empty() || filePath.empty()) {
            body["success"] = false;
            body["message"] = "videoId 和 filePath 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        body["success"] = true;
        body["message"] = "转码任务已接收";
        body["data"]["videoId"] = videoId;
        body["data"]["status"] = "PENDING";
        body["data"]["note"] = "当前为第一阶段转码服务骨架，尚未接入 HLS/FFmpeg/MQ";
        setJsonResponse(response, 202, body);
    });

    INF("transcode_service listening on 0.0.0.0:{}", settings->server.port);
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("transcode_service failed to listen on port {}", settings->server.port);
        return 1;
    }
    return 0;
}
