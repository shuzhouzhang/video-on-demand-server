#include "svc_server.h"
#include "svc_data.h"
#include "svc_sync.h"

#include "../../common/bitelog.h"
#include "../../common/config.h"
#include "../../common/object_storage.h"
#include "../../common/util.h"
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
#include "../../common/etcd_registry.h"
#endif

#include <httplib.h>
#include <jsoncpp/json/json.h>

#include <filesystem>
#include <iostream>
#include <memory>
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

void registerRoutes(httplib::Server& server,
                    const svc_file::FileDataFacade& data,
                    bool mountLocalUploads) {
    if (mountLocalUploads) {
        std::error_code ignored;
        std::filesystem::create_directories("uploads", ignored);
        server.set_mount_point("/uploads", "uploads");
    }

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
        body["service"] = "file_service";
        body["status"] = "ok";
        setJsonResponse(response, 200, body);
    });
    server.Post("/files/upload", [&data](const httplib::Request& request,
                                    httplib::Response& response) {
        Json::Value body;
        if (!request.is_multipart_form_data() || !request.has_file("file")) {
            body["success"] = false;
            body["message"] = "上传格式必须包含 file 字段";
            setJsonResponse(response, 200, body);
            return;
        }

        const auto file = request.get_file_value("file");
        std::string directory = "misc";
        if (request.has_file("directory")) {
            directory = request.get_file_value("directory").content;
        }

        svc_file::StoredFile stored;
        std::string error;
        if (!data.storeUploadedFile(directory, file.filename, file.content,
                                    stored, error)) {
            body["success"] = false;
            body["message"] = error.empty() ? "文件保存失败" : error;
            const int status = error == "文件不能为空或超过大小限制" ? 200 : 500;
            setJsonResponse(response, status, body);
            return;
        }

        body["success"] = true;
        body["message"] = "文件上传成功";
        body["storedPath"] = stored.storedPath;
        body["publicUrl"] = stored.publicUrl;
        body["originalName"] = stored.originalName;
        body["storageGroup"] = stored.storageGroup;
        body["remoteName"] = stored.remoteName;
        body["sizeBytes"] = Json::UInt64(stored.sizeBytes);
        setJsonResponse(response, 200, body);
    });

    server.Get(R"(/uploads/(.+))", [&data](const httplib::Request& request,
                                             httplib::Response& response) {
        if (request.matches.size() < 2) {
            response.status = 404;
            return;
        }
        std::string content;
        std::string error;
        if (!data.downloadStoredFile(request.matches[1].str(), content, error)) {
            Json::Value body;
            body["success"] = false;
            body["message"] = error.empty() ? "文件不存在" : error;
            setJsonResponse(response, error == "local object not found" ? 404 : 503,
                            body);
            return;
        }
        response.status = 200;
        response.set_content(std::move(content), "application/octet-stream");
    });
}

}  // namespace

namespace svc_file {

FileServerBuilder& FileServerBuilder::withConfigPath(std::string configPath) {
    configPath_ = std::move(configPath);
    return *this;
}

int FileServerBuilder::start() const {
    std::string error;
    const auto settings = biteconfig::Config::load(configPath_, error);
    if (!settings) {
        std::cerr << "file_service 启动失败: " << error << '\n';
        return 1;
    }

    bitelog::bitelog_init(settings->log);

#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    std::unique_ptr<bitesvc::EtcdServiceProvider> serviceProvider;
    if (settings->registry.enabled) {
        bitesvc::ServiceEndpoint endpoint{
            "file_service",
            "http://127.0.0.1:" + std::to_string(settings->server.port),
            "",
            "http"};
        serviceProvider = std::make_unique<bitesvc::EtcdServiceProvider>(
            settings->registry, "file_service", std::move(endpoint));
        if (!serviceProvider->start(error)) {
            ERR("file_service etcd registration failed: {}", error);
            return 1;
        }
    }
#endif

    httplib::Server server;
    CacheDelete cacheDelete;
    (void)cacheDelete;

    std::unique_ptr<bitestorage::IObjectStorage> storage;
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
    if (settings->fastdfs.enabled) {
        storage = bitestorage::makeFastDfsObjectStorage(settings->fastdfs);
    }
#endif
    if (!storage) storage = bitestorage::makeLocalObjectStorage("uploads");
    const std::string publicPrefix = settings->fastdfs.publicPathPrefix.empty()
        ? "/uploads" : settings->fastdfs.publicPathPrefix;
    FileDataFacade data(*storage, publicPrefix);
    registerRoutes(server, data, !settings->fastdfs.enabled);

    INF("file_service listening on 0.0.0.0:{}", settings->server.port);
    if (!server.listen("0.0.0.0", settings->server.port)) {
        ERR("file_service failed to listen on port {}", settings->server.port);
        return 1;
    }
    return 0;
}

}  // namespace svc_file
