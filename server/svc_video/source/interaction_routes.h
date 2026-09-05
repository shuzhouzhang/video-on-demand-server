#pragma once
#include "../../common/route_support.h"
namespace biteserver {
// 注册本服务拥有的业务接口；不挂载其他服务的路由。
void registerInteractionRoutes(httplib::Server& server, RouteContext context);
}
