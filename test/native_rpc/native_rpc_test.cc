#include "../http/fake_repositories.h"
#include "../../server/svc_user/source/native_rpc.h"
#include "../../server/svc_video/source/native_rpc.h"
#include <iostream>

int main() {
    bool ok = true;
    auto check = [&](bool value, const char* name) {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << name << '\n';
        ok &= value;
    };
    FakeRepositories repositories;
    biteserver::RouteContext context{{&repositories,&repositories,&repositories,&repositories},nullptr,true};
    svc_user::NativeUserService users(context);
    svc_video::NativeVideoService videos(repositories);
    vod::api::GetProfileRequest profile;
    vod::api::GetProfileResponse profileResult;
    profile.set_account("bit-user-001");
    users.GetProfile(nullptr,&profile,&profileResult,nullptr);
    check(profileResult.status().code()==401,"native profile rejects missing trusted identity");
    profile.mutable_context()->set_authenticated_account("other-user");
    profileResult.Clear();
    users.GetProfile(nullptr,&profile,&profileResult,nullptr);
    check(profileResult.status().code()==403,"native profile rejects forged account");
    profile.mutable_context()->set_authenticated_account("bit-user-001");
    profile.mutable_context()->set_request_id("native-profile-test");
    profileResult.Clear();
    users.GetProfile(nullptr,&profile,&profileResult,nullptr);
    check(profileResult.success() && profileResult.user().account()=="bit-user-001",
          "native profile directly reads domain repository");
    check(profileResult.status().request_id()=="native-profile-test","native response retains request ID");
    vod::api::VideoListRequest list;
    vod::api::VideoListResponse listResult;
    videos.ListVideos(nullptr,&list,&listResult,nullptr);
    check(listResult.success() && listResult.videos_size()>0,"native video list returns domain data");
    check(listResult.videos_size()>0 && listResult.videos(0).video_id()=="video-001",
          "protobuf preserves string video ID without numeric conversion");
    vod::api::VideoDetailRequest detail;
    vod::api::VideoDetailResponse detailResult;
    videos.GetVideoDetail(nullptr,&detail,&detailResult,nullptr);
    check(!detailResult.success() && detailResult.message()=="视频 id 不能为空","native detail rejects missing ID");
    detail.set_video_id("video-001"); detailResult.Clear();
    videos.GetVideoDetail(nullptr,&detail,&detailResult,nullptr);
    check(detailResult.success() && detailResult.video().video_id()=="video-001","native detail uses string ID");
    vod::api::LoginRequest login;
    vod::api::LoginResponse loginResult;
    login.set_account("missing"); login.set_password("wrong");
    users.Login(nullptr,&login,&loginResult,nullptr);
    check(!loginResult.success() && loginResult.status().code()==200,"bad credentials remain a business failure");
    return ok ? 0 : 1;
}
