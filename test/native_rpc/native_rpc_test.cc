#include "../http/fake_repositories.h"
#include "../../server/svc_user/source/native_rpc.h"
#include "../../server/svc_video/source/native_rpc.h"
#include <iostream>
#include "../../server/svc_user/source/user_operations.h"
#include "../../server/svc_video/source/video_operations.h"
#include "../../server/svc_video/source/interaction_operations.h"
#include <map>

class FakeMediaStorage : public bitestorage::IObjectStorage {
  public:
    std::map<std::string, std::string> objects;
    bool failCover = false;
    bool put(const std::string &directory, const std::string &name,
             const std::string &content, bitestorage::StoredObject &result,
             std::string &error) override {
        if (failCover && directory == "covers") {
            error = "injected cover failure";
            return false;
        }
        result.locator =
            directory + "/object-" + std::to_string(++sequence) + "-" + name;
        result.sizeBytes = content.size();
        objects[result.locator] = content;
        return true;
    }
    bool get(const std::string &id, std::string &bytes,
             std::string &) override {
        auto i = objects.find(id);
        if (i == objects.end())
            return false;
        bytes = i->second;
        return true;
    }
    bool remove(const std::string &id, std::string &) override {
        objects.erase(id);
        return true;
    }
    int sequence = 0;
};

int main() {
    bool ok = true;
    auto check = [&](bool value, const char *name) {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << name << '\n';
        ok &= value;
    };
    FakeRepositories repositories;
    biteserver::RouteContext context{
        {&repositories, &repositories, &repositories, &repositories},
        nullptr,
        true};
    svc_user::NativeUserService users(context);
    svc_video::NativeVideoService videos(repositories);
    vod::api::GetProfileRequest profile;
    vod::api::GetProfileResponse profileResult;
    profile.set_account("bit-user-001");
    users.GetProfile(nullptr, &profile, &profileResult, nullptr);
    check(profileResult.status().code() == 401,
          "native profile rejects missing trusted identity");
    profile.mutable_context()->set_authenticated_account("other-user");
    profileResult.Clear();
    users.GetProfile(nullptr, &profile, &profileResult, nullptr);
    check(profileResult.status().code() == 403,
          "native profile rejects forged account");
    profile.mutable_context()->set_authenticated_account("bit-user-001");
    profile.mutable_context()->set_request_id("native-profile-test");
    profileResult.Clear();
    users.GetProfile(nullptr, &profile, &profileResult, nullptr);
    check(profileResult.success() &&
              profileResult.user().account() == "bit-user-001",
          "native profile directly reads domain repository");
    check(profileResult.status().request_id() == "native-profile-test",
          "native response retains request ID");
    vod::api::VideoListRequest list;
    vod::api::VideoListResponse listResult;
    videos.ListVideos(nullptr, &list, &listResult, nullptr);
    check(listResult.success() && listResult.videos_size() > 0,
          "native video list returns domain data");
    check(listResult.videos_size() > 0 &&
              listResult.videos(0).video_id() == "video-001",
          "protobuf preserves string video ID without numeric conversion");
    vod::api::VideoDetailRequest detail;
    vod::api::VideoDetailResponse detailResult;
    videos.GetVideoDetail(nullptr, &detail, &detailResult, nullptr);
    check(!detailResult.success() &&
              detailResult.message() == "视频 id 不能为空",
          "native detail rejects missing ID");
    detail.set_video_id("video-001");
    detailResult.Clear();
    videos.GetVideoDetail(nullptr, &detail, &detailResult, nullptr);
    check(detailResult.success() &&
              detailResult.video().video_id() == "video-001",
          "native detail uses string ID");
    vod::api::LoginRequest login;
    vod::api::LoginResponse loginResult;
    login.set_account("missing");
    login.set_password("wrong");
    users.Login(nullptr, &login, &loginResult, nullptr);
    check(!loginResult.success() && loginResult.status().code() == 200,
          "bad credentials remain a business failure");

    FakeMediaStorage media;
    context.mediaStorage = &media;
    svc_user::UserOperations userOperations(context);
    svc_video::VideoOperations videoOperations(context);
    svc_video::InteractionOperations interactions(context);
    vod::business::SaveProgressRequest progress;
    vod::business::SaveProgressResponse progressResponse;
    progress.mutable_context()->set_authenticated_account("bit-user-001");
    progress.mutable_payload()->set_videoid("video-001");
    interactions.SaveProgress(nullptr, &progress, &progressResponse, nullptr);
    check(!progressResponse.result().success(),
          "typed RPC preserves missing numeric field validation");
    progress.mutable_payload()->set_seconds(0);
    progressResponse.Clear();
    interactions.SaveProgress(nullptr, &progress, &progressResponse, nullptr);
    check(progressResponse.result().success() &&
              progressResponse.result().has_seconds() &&
              progressResponse.result().seconds() == 0,
          "typed RPC preserves explicit zero progress");
    progress.mutable_payload()->set_account("victim");
    progressResponse.Clear();
    interactions.SaveProgress(nullptr, &progress, &progressResponse, nullptr);
    check(progressResponse.status().code() == 403,
          "typed write cannot claim another account");
    vod::business::ListUsersRequest admin;
    vod::business::ListUsersResponse adminResponse;
    admin.mutable_context()->set_authenticated_account("bit-user-001");
    userOperations.ListUsers(nullptr, &admin, &adminResponse, nullptr);
    check(adminResponse.status().code() == 403,
          "typed admin endpoint enforces role check");
    vod::business::UploadVideoRequest upload;
    vod::business::UploadVideoResponse uploaded;
    upload.mutable_context()->set_authenticated_account("bit-user-001");
    upload.mutable_payload()->set_title("RPC upload");
    upload.mutable_payload()->set_category("test");
    upload.mutable_videofile()->set_filename("source.mp4");
    upload.mutable_videofile()->set_content("source bytes");
    upload.mutable_coverfile()->set_filename("cover.png");
    upload.mutable_coverfile()->set_content("cover bytes");
    media.failCover = true;
    videoOperations.UploadVideo(nullptr, &upload, &uploaded, nullptr);
    check(uploaded.status().code() == 503 && media.objects.empty(),
          "partial media upload rolls back created source object");
    media.failCover = false;
    uploaded.Clear();
    videoOperations.UploadVideo(nullptr, &upload, &uploaded, nullptr);
    check(uploaded.result().success() &&
              uploaded.result().video().storedvideopath().rfind("object:", 0) ==
                  0,
          "typed upload stores file ID instead of local filesystem path");
    check(media.objects.size() == 2 &&
              uploaded.result().video().storedcoverpath().rfind("object:", 0) ==
                  0,
          "source and cover both pass through file storage port");
    vod::business::UploadAvatarRequest avatar;
    vod::business::UploadAvatarResponse avatarResponse;
    avatar.mutable_context()->set_authenticated_account("bit-user-001");
    avatar.mutable_avatarfile()->set_filename("avatar.png");
    avatar.mutable_avatarfile()->set_content("avatar bytes");
    userOperations.UploadAvatar(nullptr, &avatar, &avatarResponse, nullptr);
    check(avatarResponse.result().success() &&
              avatarResponse.result().avatarpath().rfind(
                  "/uploads/avatars/object-", 0) == 0,
          "avatar update references remotely stored object");

    return ok ? 0 : 1;
}
