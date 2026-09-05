#pragma once

#include "config.h"

#include <cstdint>
#include <memory>
#include <string>

namespace bitestorage {

struct StoredObject {
    std::string locator;
    std::string storageGroup;
    std::string remoteName;
    std::uint64_t sizeBytes = 0;
};

class IObjectStorage {
public:
    virtual ~IObjectStorage() = default;
    virtual bool put(const std::string& directory,
                     const std::string& originalName,
                     const std::string& content,
                     StoredObject& stored,
                     std::string& error) = 0;
    virtual bool get(const std::string& locator,
                     std::string& content,
                     std::string& error) = 0;
    virtual bool remove(const std::string& locator,
                        std::string& error) = 0;
};

std::unique_ptr<IObjectStorage> makeLocalObjectStorage(
    const std::string& uploadRoot);

#ifdef VOD_ENABLE_REFERENCE_RUNTIME
std::unique_ptr<IObjectStorage> makeFastDfsObjectStorage(
    const biteconfig::FastDfsSettings& settings);
#endif

}  // namespace bitestorage
