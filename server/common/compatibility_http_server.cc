#include "http_server.h"
#include "route_support.h"
#include "../svc_user/source/user_routes.h"
#include "../svc_video/source/video_routes.h"
#include "../svc_video/source/interaction_routes.h"
#include <utility>
namespace biteserver {
// 仅供单体入口和旧调用者使用；微服务二进制不链接该兼容装配层。
HttpServer::HttpServer(biterepo::RepositorySet repositories)
    : HttpServer(repositories, ServiceRole::All, "video_server", nullptr, false) {}
HttpServer::HttpServer(biterepo::RepositorySet repositories, ServiceRole role,
                       std::string serviceName)
    : HttpServer(repositories, role, std::move(serviceName), nullptr, false) {}
HttpServer::HttpServer(biterepo::RepositorySet repositories, ServiceRole role,
                       std::string serviceName,
                       bitesession::RedisSessionManager* sessions, bool enforce)
    : HttpServer(std::move(serviceName), [=](httplib::Server& http) {
        const RouteContext context{repositories, sessions, enforce};
        if (role == ServiceRole::All || role == ServiceRole::File) {
            std::filesystem::create_directories("uploads");
            http.set_mount_point("/uploads", "uploads");
        }
        registerSmokeRoutes(http, context);
        if (role == ServiceRole::All || role == ServiceRole::User)
            registerUserRoutes(http, context);
        if (role == ServiceRole::All || role == ServiceRole::Video)
            registerVideoRoutes(http, context);
        if (role == ServiceRole::All || role == ServiceRole::Video || role == ServiceRole::Interaction)
            registerInteractionRoutes(http, context);
    }) {}
}  // namespace biteserver
