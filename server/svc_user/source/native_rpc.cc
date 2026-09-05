#include "native_rpc.h"
#include <brpc/closure_guard.h>

namespace svc_user {
namespace {
void fillUser(const bitevideo::UserProfile& user, vod::api::UserInfo& output) {
    output.set_account(user.account);
    output.set_user_name(user.userName);
    output.set_avatar_url(user.avatarPath);
    output.set_description(user.description);
}
}
void NativeUserService::Login(google::protobuf::RpcController*,
    const vod::api::LoginRequest* request, vod::api::LoginResponse* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    auto* status = response->mutable_status();
    status->set_code(200);
    status->set_request_id(request->context().request_id());
    std::optional<bitevideo::UserProfile> user;
    std::string error;
    if (!context_.repositories.users->passwordLogin(
            biteserver::detail::trimCopy(request->account()), request->password(), user, error)) {
        status->set_code(500);
        response->set_message("登录暂时不可用");
    } else if (!user) {
        response->set_message("账号或密码错误");
    } else {
        std::string token;
        if (context_.sessions && !context_.sessions->createToken(user->account, token, error)) {
            status->set_code(500);
            response->set_message("登录状态保存失败");
            return;
        }
        response->set_success(true);
        response->set_token(token);
        fillUser(*user, *response->mutable_user());
    }
}
void NativeUserService::GetProfile(google::protobuf::RpcController*,
    const vod::api::GetProfileRequest* request, vod::api::GetProfileResponse* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    auto* status = response->mutable_status();
    status->set_code(200);
    status->set_request_id(request->context().request_id());
    std::string account = biteserver::detail::trimCopy(request->account());
    const auto& authenticated = request->context().authenticated_account();
    if (context_.enforceGatewayIdentity) {
        if (authenticated.empty()) {
            status->set_code(401); response->set_message("unauthorized"); return;
        }
        if (!account.empty() && account != authenticated) {
            status->set_code(403); response->set_message("认证账号与请求账号不一致"); return;
        }
        account = authenticated;
    }
    if (account.empty()) { response->set_message("用户不存在"); return; }
    std::optional<bitevideo::UserProfile> user;
    std::string error;
    if (!context_.repositories.users->userProfile(account, user, error)) {
        status->set_code(500); response->set_message("个人资料暂时不可用");
    } else if (!user) {
        response->set_message("用户不存在");
    } else {
        response->set_success(true);
        fillUser(*user, *response->mutable_user());
    }
}
}
