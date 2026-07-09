/*
 * HTTP 服务入口：注册公共路由，并负责监听和停止服务。
 */
#pragma once

#include "../svc_video/source/video_repository.h"

#include <cstdint>
#include <memory>
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
    explicit HttpServer(bitevideo::VideoStore& videoStore);
    HttpServer(bitevideo::VideoStore& videoStore, ServiceRole role,
               std::string serviceName);
    HttpServer(bitevideo::VideoStore& videoStore, ServiceRole role,
               std::string serviceName,
               bitesession::RedisSessionManager* sessionManager);

    // 正式运行入口；成功监听后会阻塞，直到服务被停止。
    bool listen(const std::string& host, std::uint16_t port);

    // 分步绑定接口便于自动测试使用系统分配的空闲端口。
    int bindToAnyPort(const std::string& host);
    bool listenAfterBind();
    void stop();

private:
    void registerRoutes();
    void registerHealthRoutes();

    httplib::Server server_;
    bitevideo::VideoStore& videoStore_;
    ServiceRole role_;
    std::string serviceName_;
    bitesession::RedisSessionManager* sessionManager_;
};

}  // namespace biteserver
