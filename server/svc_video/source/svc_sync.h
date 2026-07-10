#pragma once

#include "../../common/sync.h"

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace svc_video {

class CacheDelete final : public bitesync::CacheSync {
public:
    bool sync(const std::string& key, std::string& error) override;
    bool contains(const std::string& key) const;
    std::size_t deletedCount() const noexcept;

private:
    std::unordered_set<std::string> deletedKeys_;
};

class CacheToDB final : public bitesync::CacheSync {
public:
    explicit CacheToDB(CacheDelete& deleteCache);

    bool sync(const std::string& videoId, std::string& error) override;
    const std::vector<std::string>& pendingVideoIds() const noexcept;

private:
    CacheDelete& deleteCache_;
    std::vector<std::string> pendingVideoIds_;
};

}  // namespace svc_video
