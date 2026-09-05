#pragma once
#include "svc_data.h"
#include "file.pb.h"
#include <brpc/closure_guard.h>
namespace svc_file {
inline std::string contentType(const std::string &name) {
    if (name.size() >= 5 && name.substr(name.size() - 5) == ".m3u8")
        return "application/vnd.apple.mpegurl";
    if (name.size() >= 3 && name.substr(name.size() - 3) == ".ts")
        return "video/mp2t";
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".mp4")
        return "video/mp4";
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".png")
        return "image/png";
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".jpg")
        return "image/jpeg";
    return "application/octet-stream";
}
class NativeFileService final : public vod::api::FileService {
  public:
    NativeFileService(FileDataFacade &data,
                      bitestorage::IObjectStorage &storage)
        : data_(data), storage_(storage) {}
    void UploadFile(google::protobuf::RpcController *,
                    const vod::api::UploadFileRequest *request,
                    vod::api::UploadFileResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        StoredFile file;
        std::string error;
        const bool ok = data_.storeUploadedFile(
            request->directory(), request->original_name(), request->content(),
            file, error);
        response->set_success(ok);
        response->set_message(ok ? "文件上传成功" : error);
        response->mutable_status()->set_code(ok ? 200 : 500);
        response->mutable_status()->set_request_id(
            request->context().request_id());
        if (!ok)
            return;
        auto *result = response->mutable_file();
        result->set_file_id(file.storedPath);
        result->set_stored_path(file.storedPath);
        result->set_public_url(file.publicUrl);
        result->set_original_name(file.originalName);
        result->set_size(file.sizeBytes);
        result->set_storage_group(file.storageGroup);
        result->set_remote_name(file.remoteName);
        result->set_mime_type(contentType(file.originalName));
    }
    void DownloadFile(google::protobuf::RpcController *,
                      const vod::api::DownloadFileRequest *request,
                      vod::api::DownloadFileResponse *response,
                      google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        std::string content, error;
        std::string locator = request->public_url();
        if (locator.rfind("/uploads/", 0) == 0)
            locator.erase(0, 9);
        const bool ok = data_.downloadStoredFile(locator, content, error);
        response->set_success(ok);
        response->set_message(error);
        response->mutable_status()->set_code(
            ok ? 200 : (error == "local object not found" ? 404 : 503));
        response->mutable_status()->set_request_id(
            request->context().request_id());
        if (ok) {
            response->set_content(std::move(content));
            response->set_content_type(contentType(locator));
        }
    }
    void RemoveFile(google::protobuf::RpcController *,
                    const vod::api::RemoveFileRequest *request,
                    vod::api::RemoveFileResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        std::string error;
        const auto &id = request->file_id();
        const bool valid = !id.empty() && id.front() != '/' &&
                           id.find("..") == std::string::npos;
        response->set_success(valid && storage_.remove(id, error));
        response->set_message(error);
    }

  private:
    FileDataFacade &data_;
    bitestorage::IObjectStorage &storage_;
};
} // namespace svc_file
