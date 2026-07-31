#pragma once

#include "config.h"
#include "service_registry.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace bitesvc {

class IEtcdTransport {
public:
    virtual ~IEtcdTransport() = default;
    virtual bool grantLease(int ttlSeconds, std::int64_t& leaseId,
                            std::string& error) = 0;
    virtual bool keepAlive(std::int64_t leaseId, std::string& error) = 0;
    virtual bool put(const std::string& key, const std::string& value,
                     std::int64_t leaseId, std::string& error) = 0;
    virtual bool erase(const std::string& key, std::string& error) = 0;
    virtual bool listPrefix(const std::string& prefix,
                            std::vector<std::pair<std::string, std::string>>& values,
                            std::string& error) = 0;
};

std::unique_ptr<IEtcdTransport> makeCurlEtcdTransport(
    const std::string& endpoint, int timeoutMs);

class EtcdServiceProvider {
public:
    EtcdServiceProvider(biteconfig::RegistrySettings settings,
                        std::string serviceName,
                        ServiceEndpoint endpoint,
                        std::string version = "1");
    ~EtcdServiceProvider();

    bool start(std::string& error);
    void stop();
    const std::string& instanceId() const noexcept;

private:
    bool registerLease(std::string& error);
    void keepAliveLoop();

    biteconfig::RegistrySettings settings_;
    std::string serviceName_;
    ServiceEndpoint endpoint_;
    std::string version_;
    std::string instanceId_;
    std::string key_;
    std::int64_t leaseId_ = 0;
    std::unique_ptr<IEtcdTransport> transport_;
    std::atomic<bool> stopped_{true};
    std::thread worker_;
    std::mutex leaseMutex_;
};

class EtcdServiceWatcher {
public:
    EtcdServiceWatcher(biteconfig::RegistrySettings settings,
                       ServiceRegistry& registry);
    ~EtcdServiceWatcher();

    bool start(std::string& error);
    void stop();
    bool refresh(std::string& error);

private:
    void watchLoop();

    biteconfig::RegistrySettings settings_;
    ServiceRegistry& registry_;
    std::unique_ptr<IEtcdTransport> transport_;
    std::atomic<bool> stopped_{true};
    std::thread worker_;
    std::set<std::string> knownServices_;
};

}  // namespace bitesvc
