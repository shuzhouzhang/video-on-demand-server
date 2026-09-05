#pragma once

#include "config.h"

#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace bitesvc {

struct ServiceEndpoint {
    std::string name;
    std::string baseUrl;
    std::string instanceId;
    std::string protocol = "http";
};

class ServiceRegistry {
public:
    ServiceRegistry();
    bool registerService(const std::string& name,
                         const std::string& baseUrl,
                         std::string& error);
    bool replaceServiceInstances(const std::string& name,
                                 std::vector<ServiceEndpoint> endpoints,
                                 std::string& error);
    std::optional<ServiceEndpoint> resolve(const std::string& name) const;
    // Compatibility helper for old tests. New request paths must use resolve()
    // because a pointer cannot remain stable across an etcd refresh.
    const ServiceEndpoint* find(const std::string& name) const;
    std::vector<ServiceEndpoint> list() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

struct DiscoverySettings {
    ServiceRegistry registry;
    biteconfig::RedisSettings redis;
    biteconfig::RegistrySettings registrySettings;
    biteconfig::RpcSettings rpc;
    int timeoutMs = 3000;
};

bool loadDiscoverySettings(const std::string& filename,
                           DiscoverySettings& settings,
                           std::string& error);

}  // namespace bitesvc
