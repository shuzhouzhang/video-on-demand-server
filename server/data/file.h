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

}  // namespace bitefile
