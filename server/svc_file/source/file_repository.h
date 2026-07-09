#pragma once

#include "../../data/file.h"

#include <string>

namespace bitefile {

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
