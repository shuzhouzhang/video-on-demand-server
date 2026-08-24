#pragma once

#include "../../common/sync.h"

#include <cstddef>
#include <string>
#include <unordered_set>

namespace svc_file {

class CacheDelete final : public bitesync::CacheSync {
public:
    bool sync(const std::string& key, std::string& error) override;
    bool contains(const std::string& key) const;
    std::size_t deletedCount() const noexcept;

private:
    std::unordered_set<std::string> deletedKeys_;
};

}  // namespace svc_file
