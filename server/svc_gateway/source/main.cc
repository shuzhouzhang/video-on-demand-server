#include "svc_server.h"

#include <string>

int main(int argc, char* argv[]) {
    const std::string gatewayConfig =
        argc > 1 ? argv[1] : "conf/gateway.local.json";
    const std::string servicesConfig =
        argc > 2 ? argv[2] : "conf/services.local.json";
    return svc_gateway::GatewayServerBuilder()
        .withGatewayConfig(gatewayConfig)
        .withServicesConfig(servicesConfig)
        .start();
}
