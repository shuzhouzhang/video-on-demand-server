#pragma once

#include <cstddef>
#include <string>

namespace svc_file {

struct StoredFile {
    std::string originalName;
    std::string storedPath;
    std::string publicUrl;
};

class FileDataFacade {
public:
    bool storeUploadedFile(const std::string& directory,
                           const std::string& filename,
                           const std::string& content,
                           StoredFile& stored,
                           std::string& error) const;

    static constexpr std::size_t maxFileBytes() noexcept {
        return 64 * 1024 * 1024;
    }
};

}  // namespace svc_file
