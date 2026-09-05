#pragma once
#include "user.pb.h"
#include "../../common/route_support.h"
namespace svc_user {
// 强类型 RPC 直接访问用户领域，不再回环转发 HTTP。
class NativeUserService final : public vod::api::UserService {
public:
    explicit NativeUserService(biteserver::RouteContext context) : context_(context) {}
    void Login(google::protobuf::RpcController*, const vod::api::LoginRequest*,
               vod::api::LoginResponse*, google::protobuf::Closure*) override;
    void GetProfile(google::protobuf::RpcController*, const vod::api::GetProfileRequest*,
                    vod::api::GetProfileResponse*, google::protobuf::Closure*) override;
private:
    biteserver::RouteContext context_;
};
}
