#pragma once

#include "config.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bitesvc {

struct ServiceEndpoint {
    std::string name;
    std::string baseUrl;
};

class ServiceRegistry {
public:
    bool registerService(const std::string& name,
                         const std::string& baseUrl,
                         std::string& error);
    const ServiceEndpoint* find(const std::string& name) const;
    std::vector<ServiceEndpoint> list() const;

private:
    std::unordered_map<std::string, ServiceEndpoint> services_;
};

struct DiscoverySettings {
    ServiceRegistry registry;
    biteconfig::RedisSettings redis;
    int timeoutMs = 3000;
};

bool loadDiscoverySettings(const std::string& filename,
                           DiscoverySettings& settings,
                           std::string& error);

}  // namespace bitesvc
