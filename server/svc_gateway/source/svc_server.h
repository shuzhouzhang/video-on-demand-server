#pragma once

#include <string>

namespace svc_gateway {

class GatewayServerBuilder {
public:
    GatewayServerBuilder& withGatewayConfig(std::string configPath);
    GatewayServerBuilder& withServicesConfig(std::string configPath);
    int start() const;

private:
    std::string gatewayConfigPath_ = "conf/gateway.local.json";
    std::string servicesConfigPath_ = "conf/services.local.json";
};

}  // namespace svc_gateway
