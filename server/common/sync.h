#pragma once

#include <string>

namespace bitesync {

class CacheSync {
public:
    virtual ~CacheSync() = default;
    virtual bool sync(const std::string& key, std::string& error) = 0;
};

}  // namespace bitesync
