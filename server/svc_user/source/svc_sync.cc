#include "svc_sync.h"

namespace svc_user {

bool CacheDelete::sync(const std::string& key, std::string& error) {
    error.clear();
    if (key.empty()) {
        error = "缓存 key 不能为空";
        return false;
    }
    deletedKeys_.insert(key);
    return true;
}

bool CacheDelete::contains(const std::string& key) const {
    return deletedKeys_.find(key) != deletedKeys_.end();
}

std::size_t CacheDelete::deletedCount() const noexcept {
    return deletedKeys_.size();
}

}  // namespace svc_user
