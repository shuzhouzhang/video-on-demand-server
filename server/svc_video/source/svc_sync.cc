#include "svc_sync.h"

namespace svc_video {

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

CacheToDB::CacheToDB(CacheDelete& deleteCache) : deleteCache_(deleteCache) {}

bool CacheToDB::sync(const std::string& videoId, std::string& error) {
    error.clear();
    if (videoId.empty()) {
        error = "视频 id 不能为空";
        return false;
    }
    pendingVideoIds_.push_back(videoId);
    return deleteCache_.sync("vod:video:count:" + videoId, error);
}

const std::vector<std::string>& CacheToDB::pendingVideoIds() const noexcept {
    return pendingVideoIds_;
}

}  // namespace svc_video
