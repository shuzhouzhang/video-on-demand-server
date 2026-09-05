#pragma once
#include "../../common/route_support.h"
namespace biteserver {
// 同一业务处理器供 HTTP 与具名 RPC 调用；RPC 不经过监听端口或路由分发。
struct UserHandlers {
    httplib::Server::Handler Login;
    httplib::Server::Handler PasswordLogin;
    httplib::Server::Handler SendEmailCode;
    httplib::Server::Handler EmailLogin;
    httplib::Server::Handler Logout;
    httplib::Server::Handler GetProfile;
    httplib::Server::Handler UpdateProfile;
    httplib::Server::Handler UploadAvatar;
    httplib::Server::Handler ListUsers;
    httplib::Server::Handler UpdateUser;
};
UserHandlers makeUserHandlers(RouteContext context);
void registerUserRoutes(httplib::Server &server, RouteContext context);
} // namespace biteserver
