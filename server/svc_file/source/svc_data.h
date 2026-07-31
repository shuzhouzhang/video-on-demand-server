#pragma once

#include "../../common/object_storage.h"

#include <cstddef>
#include <string>

namespace svc_file {

struct StoredFile {
    std::string originalName;
    std::string storedPath;
    std::string publicUrl;
    std::string storageGroup;
    std::string remoteName;
    std::uint64_t sizeBytes = 0;
};

class FileDataFacade {
public:
    FileDataFacade(bitestorage::IObjectStorage& storage,
                   std::string publicPathPrefix);

    bool storeUploadedFile(const std::string& directory,
                           const std::string& filename,
                           const std::string& content,
                           StoredFile& stored,
                           std::string& error) const;
    bool downloadStoredFile(const std::string& locator,
                            std::string& content,
                            std::string& error) const;

    static constexpr std::size_t maxFileBytes() noexcept {
        return 200 * 1024 * 1024;
    }

private:
    bitestorage::IObjectStorage& storage_;
    std::string publicPathPrefix_;
};

}  // namespace svc_file
