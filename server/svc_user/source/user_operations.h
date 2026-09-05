#pragma once
#include "user_routes.h"
#include "../../common/business_adapter.h"
namespace svc_user {
class UserOperations final : public vod::business::UserOperations {
  public:
    explicit UserOperations(biteserver::RouteContext context)
        : handlers_(biteserver::makeUserHandlers(context)) {}
    void Login(google::protobuf::RpcController *,
               const vod::business::LoginRequest *request,
               vod::business::LoginResponse *response,
               google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.Login, false,
                                false);
    }
    void PasswordLogin(google::protobuf::RpcController *,
                       const vod::business::PasswordLoginRequest *request,
                       vod::business::PasswordLoginResponse *response,
                       google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.PasswordLogin,
                                false, false);
    }
    void SendEmailCode(google::protobuf::RpcController *,
                       const vod::business::SendEmailCodeRequest *request,
                       vod::business::SendEmailCodeResponse *response,
                       google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.SendEmailCode,
                                false, false);
    }
    void EmailLogin(google::protobuf::RpcController *,
                    const vod::business::EmailLoginRequest *request,
                    vod::business::EmailLoginResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.EmailLogin,
                                false, false);
    }
    void Logout(google::protobuf::RpcController *,
                const vod::business::LogoutRequest *request,
                vod::business::LogoutResponse *response,
                google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.Logout, false,
                                false);
    }
    void GetProfile(google::protobuf::RpcController *,
                    const vod::business::GetProfileRequest *request,
                    vod::business::GetProfileResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.GetProfile, true,
                                false);
    }
    void UpdateProfile(google::protobuf::RpcController *,
                       const vod::business::UpdateProfileRequest *request,
                       vod::business::UpdateProfileResponse *response,
                       google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.UpdateProfile,
                                false, false);
    }
    void UploadAvatar(google::protobuf::RpcController *,
                      const vod::business::UploadAvatarRequest *request,
                      vod::business::UploadAvatarResponse *response,
                      google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.UploadAvatar,
                                false, true);
    }
    void ListUsers(google::protobuf::RpcController *,
                   const vod::business::ListUsersRequest *request,
                   vod::business::ListUsersResponse *response,
                   google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.ListUsers, true,
                                false);
    }
    void UpdateUser(google::protobuf::RpcController *,
                    const vod::business::UpdateUserRequest *request,
                    vod::business::UpdateUserResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.UpdateUser,
                                false, false);
    }

  private:
    biteserver::UserHandlers handlers_;
};
} // namespace svc_user
