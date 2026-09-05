#include "remote_object_storage.h"
#include "file.pb.h"
#include <brpc/channel.h>
#include <brpc/controller.h>
namespace bitestorage {
namespace {
template <class Request, class Response, class Method>
bool callFile(bitesvc::ServiceRegistry &registry, int timeout, Request &request,
              Response &response, Method method, std::string &error) {
    const auto endpoint = registry.resolve("file_service");
    if (!endpoint || endpoint->protocol != "brpc") {
        error = "FileService is unavailable";
        return false;
    }
    auto address = endpoint->baseUrl;
    if (address.rfind("http://", 0) == 0)
        address.erase(0, 7);
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = timeout;
    options.connect_timeout_ms = timeout;
    options.max_retry = 0;
    if (channel.Init(address.c_str(), &options) != 0) {
        error = "FileService channel failed";
        return false;
    }
    vod::api::FileService_Stub stub(&channel);
    brpc::Controller controller;
    (stub.*method)(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.message();
        return false;
    }
    return true;
}
} // namespace
RemoteObjectStorage::RemoteObjectStorage(biteconfig::RegistrySettings settings,
                                         int timeoutMs)
    : watcher_(std::move(settings), registry_), timeoutMs_(timeoutMs) {}
bool RemoteObjectStorage::start(std::string &error) {
    return watcher_.start(error);
}
bool RemoteObjectStorage::put(const std::string &directory,
                              const std::string &name,
                              const std::string &content, StoredObject &stored,
                              std::string &error) {
    vod::api::UploadFileRequest request;
    vod::api::UploadFileResponse response;
    request.set_directory(directory);
    request.set_original_name(name);
    request.set_content(content);
    if (!callFile(registry_, timeoutMs_, request, response,
                  &vod::api::FileService_Stub::UploadFile, error))
        return false;
    stored.locator = response.file().file_id();
    stored.storageGroup = response.file().storage_group();
    stored.remoteName = response.file().remote_name();
    stored.sizeBytes = response.file().size();
    return true;
}
bool RemoteObjectStorage::get(const std::string &id, std::string &content,
                              std::string &error) {
    vod::api::DownloadFileRequest request;
    vod::api::DownloadFileResponse response;
    request.set_public_url(id);
    if (!callFile(registry_, timeoutMs_, request, response,
                  &vod::api::FileService_Stub::DownloadFile, error))
        return false;
    content = response.content();
    return true;
}
bool RemoteObjectStorage::remove(const std::string &id, std::string &error) {
    vod::api::RemoveFileRequest request;
    vod::api::RemoveFileResponse response;
    request.set_file_id(id);
    return callFile(registry_, timeoutMs_, request, response,
                    &vod::api::FileService_Stub::RemoveFile, error);
}
} // namespace bitestorage
