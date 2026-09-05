#pragma once
#include "../../common/route_support.h"
namespace biteserver {
// 同一业务处理器供 HTTP 与具名 RPC 调用；RPC 不经过监听端口或路由分发。
struct VideoHandlers {
    httplib::Server::Handler ListVideos;
    httplib::Server::Handler CreateVideo;
    httplib::Server::Handler UploadVideo;
    httplib::Server::Handler GetDetail;
    httplib::Server::Handler Search;
    httplib::Server::Handler GetPlayUrl;
    httplib::Server::Handler ListOwnerVideos;
    httplib::Server::Handler ListReviews;
    httplib::Server::Handler ReviewVideo;
};
VideoHandlers makeVideoHandlers(RouteContext context);
void registerVideoRoutes(httplib::Server &server, RouteContext context);
} // namespace biteserver
