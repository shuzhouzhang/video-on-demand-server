#pragma once

#include <string>

namespace bitefile {

struct StoredFile {
    std::string fieldName;
    std::string originalName;
    std::string storedPath;
    std::string publicUrl;
    std::string contentType;
};

class FileRepository {
public:
    virtual ~FileRepository() = default;
};

class LocalFileRepository final : public FileRepository {
public:
    explicit LocalFileRepository(std::string uploadRoot);

    const std::string& uploadRoot() const;

private:
    std::string uploadRoot_;
};

}  // namespace bitefile
