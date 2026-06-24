/*
 * HTTP 服务入口：注册公共路由，并负责监听和停止服务。
 */
#pragma once

#include "video_repository.h"

#include <cstdint>
#include <string>

#include <httplib.h>

namespace biteserver {

class HttpServer {
public:
    explicit HttpServer(bitevideo::VideoStore& videoStore);

    // 正式运行入口；成功监听后会阻塞，直到服务被停止。
    bool listen(const std::string& host, std::uint16_t port);

    // 分步绑定接口便于自动测试使用系统分配的空闲端口。
    int bindToAnyPort(const std::string& host);
    bool listenAfterBind();
    void stop();

private:
    void registerRoutes();

    httplib::Server server_;
    bitevideo::VideoStore& videoStore_;
};

}  // namespace biteserver
