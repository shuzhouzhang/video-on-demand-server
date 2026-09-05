#pragma once
#include "../../common/route_support.h"
namespace biteserver {
// 同一业务处理器供 HTTP 与具名 RPC 调用；RPC 不经过监听端口或路由分发。
struct InteractionHandlers {
    httplib::Server::Handler GetLike;
    httplib::Server::Handler Like;
    httplib::Server::Handler Unlike;
    httplib::Server::Handler GetProgress;
    httplib::Server::Handler SaveProgress;
    httplib::Server::Handler GetFavorite;
    httplib::Server::Handler Favorite;
    httplib::Server::Handler Unfavorite;
    httplib::Server::Handler ListFavorites;
    httplib::Server::Handler ListComments;
    httplib::Server::Handler AddComment;
    httplib::Server::Handler ListBarrages;
    httplib::Server::Handler AddBarrage;
};
InteractionHandlers makeInteractionHandlers(RouteContext context);
void registerInteractionRoutes(httplib::Server &server, RouteContext context);
} // namespace biteserver
