#include "svc_data.h"

#include <filesystem>

namespace {

std::string pathFileName(const std::string& filename) {
    return std::filesystem::path(filename).filename().string();
}

}  // namespace

namespace svc_file {

FileDataFacade::FileDataFacade(bitestorage::IObjectStorage& storage,
                               std::string publicPathPrefix,
                               bitestorage::IObjectStorage* legacyStorage)
    : storage_(storage),
      legacyStorage_(legacyStorage),
      publicPathPrefix_(std::move(publicPathPrefix)) {
    while (publicPathPrefix_.size() > 1 && publicPathPrefix_.back() == '/') {
        publicPathPrefix_.pop_back();
    }
}

bool FileDataFacade::storeUploadedFile(const std::string& directory,
                                       const std::string& filename,
                                       const std::string& content,
                                       StoredFile& stored,
                                       std::string& error) const {
    const std::string originalName = pathFileName(filename);
    if (originalName.empty() || content.empty() ||
        content.size() > maxFileBytes()) {
        error = "文件不能为空或超过大小限制";
        return false;
    }

    bitestorage::StoredObject object;
    if (!storage_.put(directory, originalName, content, object, error)) {
        return false;
    }

    stored.originalName = originalName;
    stored.storedPath = object.locator;
    stored.publicUrl = publicPathPrefix_ + "/" + object.locator;
    stored.storageGroup = object.storageGroup;
    stored.remoteName = object.remoteName;
    stored.sizeBytes = object.sizeBytes;
    return true;
}

bool FileDataFacade::downloadStoredFile(const std::string& locator,
                                        std::string& content,
                                        std::string& error) const {
    if (locator.empty() || locator.find("..") != std::string::npos ||
        locator.front() == '/') {
        error = "invalid object locator";
        return false;
    }
    const bool fastDfsLocator = locator.rfind("group", 0) == 0 &&
        locator.find("/M") != std::string::npos;
    if (!fastDfsLocator && legacyStorage_) {
        return legacyStorage_->get(locator, content, error);
    }
    return storage_.get(locator, content, error);
}

}  // namespace svc_file
