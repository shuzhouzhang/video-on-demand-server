/*
 * HTTP 服务入口：注册公共路由，并负责监听和停止服务。
 */
#pragma once

#include "../repository/repository.h"

#include <cstdint>
#include <functional>
#include <string>

#include <httplib.h>

namespace bitesession {
class RedisSessionManager;
}

namespace biteserver {

enum class ServiceRole {
    All,
    User,
    Video,
    Interaction,
    File,
};

class HttpServer {
public:
    using RouteRegistrar = std::function<void(httplib::Server&)>;
    HttpServer(std::string serviceName, const RouteRegistrar& registrar);
    explicit HttpServer(biterepo::RepositorySet repositories);
    HttpServer(biterepo::RepositorySet repositories, ServiceRole role,
               std::string serviceName);
    HttpServer(biterepo::RepositorySet repositories, ServiceRole role,
               std::string serviceName,
               bitesession::RedisSessionManager* sessionManager,
               bool enforceGatewayIdentity);

    // 正式运行入口；成功监听后会阻塞，直到服务被停止。
    bool listen(const std::string& host, std::uint16_t port);

    // 分步绑定接口便于自动测试使用系统分配的空闲端口。
    int bindToAnyPort(const std::string& host);
    bool listenAfterBind();
    void stop();

private:
    void registerHealthRoutes();

    httplib::Server server_;
    std::string serviceName_;
};

}  // namespace biteserver
