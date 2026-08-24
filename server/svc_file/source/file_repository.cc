#include "file_repository.h"

#include <utility>

namespace bitefile {

LocalFileRepository::LocalFileRepository(std::string uploadRoot)
    : uploadRoot_(std::move(uploadRoot)) {
}

const std::string& LocalFileRepository::uploadRoot() const {
    return uploadRoot_;
}

}  // namespace bitefile
