#include "../../server/common/http_server.h"
#include "fake_repositories.h"
#include "../../server/common/redis_session_manager.h"
#include "../../server/common/util.h"
#include "../../server/svc_user/source/user_routes.h"
#include "../../server/svc_video/source/video_routes.h"
#include "../../server/svc_video/source/interaction_routes.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    std::cout << "[PASS] " << message << '\n';
    return true;
}



}  // namespace

int main() {
    bool ok = true;
    std::filesystem::remove_all("uploads");
    FakeRepositories videoStore;
    const biterepo::RepositorySet allRepositories{
        &videoStore, &videoStore, &videoStore, &videoStore};
    // 即使误传完整 RepositorySet，显式装配也不能暴露另一个服务的接口。
    // 凭文件访问的否定检查，验证业务服务不再绕过 file_service 提供文件。
    std::filesystem::create_directories("uploads");
    std::ofstream("uploads/boundary-probe.txt") << "private probe";
    const biteserver::RouteContext context{allRepositories, nullptr, true};
    for (const bool userService : {true, false}) {
        biteserver::HttpServer isolated(userService ? "user_service" : "video_service",
            [context, userService](httplib::Server& http) {
                if (userService) {
                    biteserver::registerUserRoutes(http, context);
                } else {
                    biteserver::registerVideoRoutes(http, context);
                    biteserver::registerInteractionRoutes(http, context);
                }
            });
        const int isolatedPort = isolated.bindToAnyPort("127.0.0.1");
        if (!expect(isolatedPort > 0, "bind isolated service")) return 1;
        std::thread isolatedThread([&isolated] { isolated.listenAfterBind(); });
        httplib::Client isolatedClient("127.0.0.1", isolatedPort);
        const auto health = isolatedClient.Get("/healthz");
        const auto foreign = userService ? isolatedClient.Get("/videos") :
            isolatedClient.Post("/login", "{}", "application/json");
        const auto owned = userService ? isolatedClient.Get("/users/profile") :
            isolatedClient.Get("/videos");
        const auto file = isolatedClient.Get("/uploads/boundary-probe.txt");
        const auto protectedCall = userService ?
            isolatedClient.Post("/users/profile", "{}", "application/json") :
            isolatedClient.Post("/videos/like", "{}", "application/json");
        ok &= expect(health && health->status == 200,
                     "isolated service exposes health endpoint");
        ok &= expect(foreign && foreign->status == 404,
                     "isolated service does not register foreign business routes");
        ok &= expect(owned && owned->status != 404,
                     "isolated service registers owned business routes");
        ok &= expect(file && file->status == 404,
                     "business service cannot serve uploads directly");
        ok &= expect(protectedCall && protectedCall->status == 401,
                     "extracted protected routes still require gateway identity");
        isolated.stop();
        isolatedThread.join();
    }
    std::filesystem::remove("uploads/boundary-probe.txt");
    biteserver::HttpServer server(allRepositories);
    const int port = server.bindToAnyPort("127.0.0.1");
    ok &= expect(port > 0, "bind an available local port");
    if (port <= 0) {
        return 1;
    }

    std::thread serverThread([&server]() { server.listenAfterBind(); });

    httplib::Client client("127.0.0.1", port);
    const auto health = client.Get("/health");
    ok &= expect(health && health->status == 200,
                 "GET /health returns HTTP 200");
    ok &= expect(health &&
                     health->get_header_value("Content-Type").find(
                         "application/json") != std::string::npos,
                 "GET /health returns JSON");

    if (health) {
        const auto body = biteutil::JSON::unserialize(health->body);
        ok &= expect(body && (*body)["code"].asInt() == 0 &&
                         (*body)["data"]["status"].asString() == "UP",
                     "GET /health returns healthy status");
    }

    const auto missing = client.Get("/missing");
    ok &= expect(missing && missing->status == 404,
                 "unknown route returns HTTP 404");

    const auto login = client.Post(
        "/login", R"({"account":"bit-user-001","password":"123456"})",
        "application/json");
    if (login) {
        const auto body = biteutil::JSON::unserialize(login->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["account"].asString() == "bit-user-001",
                     "POST /login accepts valid credentials");
    }

    const auto badLogin = client.Post(
        "/login", R"({"account":"bit-user-001","password":"bad"})",
        "application/json");
    if (badLogin) {
        const auto body = biteutil::JSON::unserialize(badLogin->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /login rejects invalid credentials");
    }

    const auto emailCode = client.Post(
        "/login/email-code", R"({"email":"email-user@example.com"})",
        "application/json");
    if (emailCode) {
        const auto body = biteutil::JSON::unserialize(emailCode->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["authcodeId"].asString() == "email-code-001" &&
                         !body->isMember("debugCode"),
                     "POST /login/email-code hides debug code by default");
    }

#if defined(_WIN32)
    _putenv_s("VIDEO_ENABLE_EMAIL_DEBUG_CODE", "1");
#else
    setenv("VIDEO_ENABLE_EMAIL_DEBUG_CODE", "1", 1);
#endif
    const auto developmentEmailCode = client.Post(
        "/login/email-code", R"({"email":"email-user@example.com"})",
        "application/json");
    if (developmentEmailCode) {
        const auto body =
            biteutil::JSON::unserialize(developmentEmailCode->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["debugCode"].asString() == "246810",
                     "POST /login/email-code exposes debug code only by opt-in");
    }
#if defined(_WIN32)
    _putenv_s("VIDEO_ENABLE_EMAIL_DEBUG_CODE", "");
#else
    unsetenv("VIDEO_ENABLE_EMAIL_DEBUG_CODE");
#endif

    const auto emailLogin = client.Post(
        "/login/email",
        R"({"email":"email-user@example.com","authcodeId":"email-code-001","authcode":"246810"})",
        "application/json");
    if (emailLogin) {
        const auto body = biteutil::JSON::unserialize(emailLogin->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["account"].asString() == "email-user@example.com",
                     "POST /login/email accepts a valid code");
    }

    const auto repeatedEmailLogin = client.Post(
        "/login/email",
        R"({"email":"email-user@example.com","authcodeId":"email-code-001","authcode":"246810"})",
        "application/json");
    if (repeatedEmailLogin) {
        const auto body = biteutil::JSON::unserialize(repeatedEmailLogin->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /login/email consumes a code only once");
    }

    const auto badEmailLogin = client.Post(
        "/login/email",
        R"({"email":"email-user@example.com","authcodeId":"email-code-001","authcode":"000000"})",
        "application/json");
    if (badEmailLogin) {
        const auto body = biteutil::JSON::unserialize(badEmailLogin->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /login/email rejects an invalid code");
    }

    const auto limitedCode = client.Post(
        "/login/email-code", R"({"email":"limited@example.com"})",
        "application/json");
    (void)limitedCode;
    for (int attempt = 0; attempt < 5; ++attempt) {
        client.Post(
            "/login/email",
            R"({"email":"limited@example.com","authcodeId":"email-code-001","authcode":"000000"})",
            "application/json");
    }
    const auto afterAttemptLimit = client.Post(
        "/login/email",
        R"({"email":"limited@example.com","authcodeId":"email-code-001","authcode":"246810"})",
        "application/json");
    if (afterAttemptLimit) {
        const auto body = biteutil::JSON::unserialize(afterAttemptLimit->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /login/email enforces the failed-attempt limit");
    }

    const auto logout = client.Post(
        "/logout", R"({"account":"bit-user-001"})", "application/json");
    if (logout) {
        const auto body = biteutil::JSON::unserialize(logout->body);
        ok &= expect(body && (*body)["success"].asBool(),
                     "POST /logout accepts a known user");
    }

    const auto videos = client.Get("/videos");
    ok &= expect(videos && videos->status == 200,
                 "GET /videos returns HTTP 200");
    if (videos) {
        const auto body = biteutil::JSON::unserialize(videos->body);
        ok &= expect(body && body->isArray() && body->size() == 1,
                     "GET /videos returns a JSON array");
        ok &= expect(body && (*body)[0]["id"].asString() == "video-001" &&
                         (*body)[0]["duration"].asString() == "09:18" &&
                         (*body)[0]["tags"].isArray(),
                     "GET /videos matches the client contract");
    }

    const auto createdVideo = client.Post(
        "/videos",
        R"({"title":"新发布视频","account":"bit-user-001","userName":"BIT 用户","category":"科技","tags":["后端"],"description":"元数据发布","videoFileName":"new-video.mp4","coverFileName":"new-cover.jpg"})",
        "application/json");
    if (createdVideo) {
        const auto body = biteutil::JSON::unserialize(createdVideo->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["video"]["id"].asString() == "video-003" &&
                         (*body)["video"]["videoFileName"].asString() ==
                             "new-video.mp4",
                     "POST /videos creates a metadata-only video");
    }

    const auto missingVideoTitle = client.Post(
        "/videos",
        R"({"account":"bit-user-001","category":"科技","videoFileName":"new-video.mp4"})",
        "application/json");
    if (missingVideoTitle) {
        const auto body = biteutil::JSON::unserialize(missingVideoTitle->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos rejects a missing title");
    }

    const auto missingVideoAccount = client.Post(
        "/videos",
        R"({"title":"新发布视频","category":"科技","videoFileName":"new-video.mp4"})",
        "application/json");
    if (missingVideoAccount) {
        const auto body = biteutil::JSON::unserialize(
            missingVideoAccount->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos rejects a missing account");
    }

    const auto missingVideoCategory = client.Post(
        "/videos",
        R"({"title":"新发布视频","account":"bit-user-001","videoFileName":"new-video.mp4"})",
        "application/json");
    if (missingVideoCategory) {
        const auto body = biteutil::JSON::unserialize(
            missingVideoCategory->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos rejects a missing category");
    }

    const auto missingVideoFileName = client.Post(
        "/videos",
        R"({"title":"新发布视频","account":"bit-user-001","category":"科技"})",
        "application/json");
    if (missingVideoFileName) {
        const auto body = biteutil::JSON::unserialize(
            missingVideoFileName->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos rejects a missing video file name");
    }

    httplib::MultipartFormDataItems uploadItems = {
        {"metadata",
         R"({"title":"上传视频","account":"bit-user-001","userName":"BIT 用户","category":"科技","tags":["上传","后端"],"description":"multipart 上传","videoFileName":"client-name.mp4","coverFileName":"client-cover.jpg"})",
         "", "application/json"},
        {"videoFile", "fake-mp4-content", "sample.mp4", "video/mp4"},
        {"coverFile", "fake-jpg-content", "cover.jpg", "image/jpeg"}};
    const auto uploadedVideo = client.Post("/videos/upload", uploadItems);
    std::string uploadedId;
    std::string uploadedPath;
    std::string uploadedPlayPath;
    if (uploadedVideo) {
        const auto body = biteutil::JSON::unserialize(uploadedVideo->body);
        uploadedId = body ? (*body)["video"]["id"].asString() : "";
        uploadedPath = body ? (*body)["video"]["storedVideoPath"].asString() :
            "";
        uploadedPlayPath = body ? (*body)["video"]["playUrl"].asString() : "";
        ok &= expect(body && (*body)["success"].asBool() &&
                         !uploadedId.empty() &&
                         (*body)["video"]["videoFileName"].asString() ==
                             "client-name.mp4" &&
                         !uploadedPath.empty() &&
                         uploadedPlayPath == "/" + uploadedPath &&
                         !(*body)["video"]["storedCoverPath"].asString().empty(),
                      "POST /videos/upload saves file and metadata");
    }

    const auto uploadedAgain = client.Post("/videos/upload", uploadItems);
    std::string secondUploadedPath;
    if (uploadedAgain) {
        const auto body = biteutil::JSON::unserialize(uploadedAgain->body);
        secondUploadedPath = body
            ? (*body)["video"]["storedVideoPath"].asString() : "";
        ok &= expect(
            body && (*body)["success"].asBool() &&
                !secondUploadedPath.empty() &&
                secondUploadedPath != uploadedPath &&
                std::filesystem::exists(uploadedPath) &&
                std::filesystem::exists(secondUploadedPath),
            "same-account same-name uploads use different stored paths");
    }

    if (!uploadedId.empty() && !uploadedPath.empty()) {
        const auto uploadedPlayUrl = client.Get(
            ("/videos/play-url?videoId=" + uploadedId).c_str());
        if (uploadedPlayUrl) {
            const auto body = biteutil::JSON::unserialize(
                uploadedPlayUrl->body);
            ok &= expect(body && !(*body)["success"].asBool(),
                         "pending upload has no public play URL");
        }
    }

    if (!uploadedPlayPath.empty()) {
        const auto uploadedFile = client.Get(uploadedPlayPath.c_str());
        ok &= expect(uploadedFile && uploadedFile->status == 200 &&
                         uploadedFile->body == "fake-mp4-content",
                     "GET /uploads/... serves uploaded video bytes");
    }

    httplib::MultipartFormDataItems missingUploadFile = {
        {"metadata",
         R"({"title":"上传视频","account":"bit-user-001","category":"科技"})",
         "", "application/json"}};
    const auto missingUpload = client.Post("/videos/upload", missingUploadFile);
    if (missingUpload) {
        const auto body = biteutil::JSON::unserialize(missingUpload->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/upload rejects missing video file");
    }

    httplib::MultipartFormDataItems badUploadSuffix = {
        {"metadata",
         R"({"title":"上传视频","account":"bit-user-001","category":"科技"})",
         "", "application/json"},
        {"videoFile", "not-a-video", "sample.txt", "text/plain"}};
    const auto badUpload = client.Post("/videos/upload", badUploadSuffix);
    if (badUpload) {
        const auto body = biteutil::JSON::unserialize(badUpload->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/upload rejects unsupported suffixes");
    }

    const auto detail = client.Get("/videos/detail?id=video-001");
    ok &= expect(detail && detail->status == 200,
                 "GET /videos/detail returns HTTP 200");
    if (detail) {
        const auto body = biteutil::JSON::unserialize(detail->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["video"]["id"].asString() == "video-001",
                     "GET /videos/detail returns the requested video");
    }

    const auto missingId = client.Get("/videos/detail");
    if (missingId) {
        const auto body = biteutil::JSON::unserialize(missingId->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "GET /videos/detail rejects a missing id");
    }

    const auto unknown = client.Get("/videos/detail?id=missing-video");
    if (unknown) {
        const auto body = biteutil::JSON::unserialize(unknown->body);
        ok &= expect(body && !(*body)["success"].asBool() &&
                         !(*body)["message"].asString().empty(),
                     "GET /videos/detail reports an unknown video");
    }

    const auto search = client.Get("/videos/search?keyword=%E6%B5%8B%E8%AF%95");
    if (search) {
        const auto body = biteutil::JSON::unserialize(search->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["videos"].isArray() &&
                         (*body)["videos"].size() == 1,
                     "GET /videos/search returns matched videos");
    }

    const auto emptySearch = client.Get("/videos/search");
    if (emptySearch) {
        const auto body = biteutil::JSON::unserialize(emptySearch->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "GET /videos/search rejects an empty keyword");
    }

    const auto noSearchResult = client.Get("/videos/search?keyword=not-found");
    if (noSearchResult) {
        const auto body = biteutil::JSON::unserialize(noSearchResult->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["videos"].isArray() &&
                         (*body)["videos"].empty(),
                     "GET /videos/search returns an empty list when no video matches");
    }

    const auto playUrl = client.Get("/videos/play-url?videoId=video-001");
    if (playUrl) {
        const auto body = biteutil::JSON::unserialize(playUrl->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["videoId"].asString() == "video-001" &&
                         !(*body)["playUrl"].asString().empty(),
                     "GET /videos/play-url returns a playable URL");
    }

    const auto missingPlayUrl = client.Get("/videos/play-url?videoId=missing");
    if (missingPlayUrl) {
        const auto body = biteutil::JSON::unserialize(missingPlayUrl->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "GET /videos/play-url reports an unknown video");
    }

    const auto initialLike = client.Get(
        "/videos/like-status?videoId=video-001&account=bit-user-001");
    if (initialLike) {
        const auto body = biteutil::JSON::unserialize(initialLike->body);
        ok &= expect(body && !(*body)["liked"].asBool() &&
                         (*body)["likeCount"].asString() == "256",
                     "like status starts unliked");
    }

    const std::string likeBody =
        R"({"videoId":"video-001","account":"bit-user-001"})";
    const auto liked = client.Post("/videos/like", likeBody, "application/json");
    if (liked) {
        const auto body = biteutil::JSON::unserialize(liked->body);
        ok &= expect(body && (*body)["liked"].asBool() &&
                         (*body)["likeCount"].asString() == "257",
                     "POST /videos/like increments once");
    }

    const auto repeated = client.Post(
        "/videos/like", likeBody, "application/json");
    if (repeated) {
        const auto body = biteutil::JSON::unserialize(repeated->body);
        ok &= expect(body && (*body)["likeCount"].asString() == "257",
                     "repeated like does not increment twice");
    }

    const auto unliked = client.Post(
        "/videos/unlike", likeBody, "application/json");
    if (unliked) {
        const auto body = biteutil::JSON::unserialize(unliked->body);
        ok &= expect(body && !(*body)["liked"].asBool() &&
                         (*body)["likeCount"].asString() == "256",
                     "POST /videos/unlike decrements once");
    }

    const auto initialProgress = client.Get(
        "/videos/watch-progress?videoId=video-001&account=bit-user-001");
    if (initialProgress) {
        const auto body = biteutil::JSON::unserialize(initialProgress->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["seconds"].asInt() == 0,
                     "watch progress defaults to zero");
    }

    const std::string progressBody =
        R"({"videoId":"video-001","account":"bit-user-001","seconds":12})";
    const auto savedProgress = client.Post(
        "/videos/watch-progress", progressBody, "application/json");
    if (savedProgress) {
        const auto body = biteutil::JSON::unserialize(savedProgress->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["seconds"].asInt() == 12,
                     "POST /videos/watch-progress saves seconds");
    }

    const auto loadedProgress = client.Get(
        "/videos/watch-progress?videoId=video-001&account=bit-user-001");
    if (loadedProgress) {
        const auto body = biteutil::JSON::unserialize(loadedProgress->body);
        ok &= expect(body && (*body)["seconds"].asInt() == 12,
                     "saved watch progress can be loaded");
    }

    const auto invalidProgress = client.Post(
        "/videos/watch-progress",
        R"({"videoId":"video-001","account":"bit-user-001","seconds":-1})",
        "application/json");
    if (invalidProgress) {
        const auto body = biteutil::JSON::unserialize(invalidProgress->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/watch-progress rejects invalid seconds");
    }

    const auto missingProgress = client.Post(
        "/videos/watch-progress",
        R"({"account":"bit-user-001","seconds":12})",
        "application/json");
    if (missingProgress) {
        const auto body = biteutil::JSON::unserialize(missingProgress->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/watch-progress rejects a missing video id");
    }

    const auto initialFavorite = client.Get(
        "/videos/favorite-status?videoId=video-001&account=bit-user-001");
    if (initialFavorite) {
        const auto body = biteutil::JSON::unserialize(initialFavorite->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         !(*body)["favorited"].asBool(),
                     "favorite status starts false");
    }

    const std::string favoriteBody =
        R"({"videoId":"video-001","account":"bit-user-001"})";
    const auto favorited = client.Post(
        "/videos/favorite", favoriteBody, "application/json");
    if (favorited) {
        const auto body = biteutil::JSON::unserialize(favorited->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["favorited"].asBool(),
                     "POST /videos/favorite marks the video as favorited");
    }

    const auto repeatedFavorite = client.Post(
        "/videos/favorite", favoriteBody, "application/json");
    if (repeatedFavorite) {
        const auto body = biteutil::JSON::unserialize(repeatedFavorite->body);
        ok &= expect(body && (*body)["favorited"].asBool(),
                     "repeated favorite stays favorited");
    }

    const auto unfavorited = client.Post(
        "/videos/unfavorite", favoriteBody, "application/json");
    if (unfavorited) {
        const auto body = biteutil::JSON::unserialize(unfavorited->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         !(*body)["favorited"].asBool(),
                     "POST /videos/unfavorite clears favorite status");
    }

    const auto missingFavoriteAccount = client.Post(
        "/videos/favorite", R"({"videoId":"video-001"})",
        "application/json");
    if (missingFavoriteAccount) {
        const auto body = biteutil::JSON::unserialize(
            missingFavoriteAccount->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/favorite requires an account");
    }

    const auto favoriteListAfterUnfavorite = client.Get(
        "/users/favorites?account=bit-user-001");
    if (favoriteListAfterUnfavorite) {
        const auto body = biteutil::JSON::unserialize(
            favoriteListAfterUnfavorite->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["videos"].isArray() &&
                         (*body)["videos"].empty(),
                     "GET /users/favorites starts empty");
    }

    const auto favoriteAgain = client.Post(
        "/videos/favorite", favoriteBody, "application/json");
    if (favoriteAgain) {
        const auto favoriteList = client.Get(
            "/users/favorites?account=bit-user-001");
        if (favoriteList) {
            const auto body = biteutil::JSON::unserialize(favoriteList->body);
            ok &= expect(body && (*body)["success"].asBool() &&
                             (*body)["videos"].size() == 1 &&
                             (*body)["videos"][0]["id"].asString() ==
                                 "video-001",
                         "GET /users/favorites returns favorited videos");
        }
    }

    const auto missingFavoriteListAccount = client.Get("/users/favorites");
    if (missingFavoriteListAccount) {
        const auto body = biteutil::JSON::unserialize(
            missingFavoriteListAccount->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "GET /users/favorites requires an account");
    }

    const auto myVideos = client.Get("/users/videos?account=bit-user-001");
    if (myVideos) {
        const auto body = biteutil::JSON::unserialize(myVideos->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["videos"].isArray() &&
                         (*body)["videos"].size() >= 1 &&
                         (*body)["videos"][0]["id"].asString() == "video-001",
                     "GET /users/videos returns owner videos");
    }

    const auto emptyMyVideos = client.Get("/users/videos?account=other-user");
    if (emptyMyVideos) {
        const auto body = biteutil::JSON::unserialize(emptyMyVideos->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["videos"].isArray() &&
                         (*body)["videos"].empty(),
                     "GET /users/videos returns an empty list when no video belongs to the account");
    }

    const auto missingMyVideosAccount = client.Get("/users/videos");
    if (missingMyVideosAccount) {
        const auto body = biteutil::JSON::unserialize(
            missingMyVideosAccount->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "GET /users/videos requires an account");
    }

    const auto initialComments = client.Get(
        "/videos/comments?videoId=video-001");
    if (initialComments) {
        const auto body = biteutil::JSON::unserialize(initialComments->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["comments"].isArray() &&
                         (*body)["comments"].empty(),
                     "GET /videos/comments starts with an empty list");
    }

    const std::string commentBody =
        R"({"videoId":"video-001","userName":"测试用户","account":"bit-user-001","content":"这是一条测试评论"})";
    const auto sentComment = client.Post(
        "/videos/comments", commentBody, "application/json");
    if (sentComment) {
        const auto body = biteutil::JSON::unserialize(sentComment->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["comment"]["content"].asString() ==
                             "这是一条测试评论",
                     "POST /videos/comments returns the saved comment");
    }

    const auto loadedComments = client.Get(
        "/videos/comments?videoId=video-001");
    if (loadedComments) {
        const auto body = biteutil::JSON::unserialize(loadedComments->body);
        ok &= expect(body && (*body)["comments"].size() == 1,
                     "GET /videos/comments returns saved comments");
    }

    const auto missingCommentLogin = client.Post(
        "/videos/comments",
        R"({"videoId":"video-001","content":"未登录评论"})",
        "application/json");
    if (missingCommentLogin) {
        const auto body = biteutil::JSON::unserialize(
            missingCommentLogin->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/comments requires login identity");
    }

    const auto emptyComment = client.Post(
        "/videos/comments",
        R"({"videoId":"video-001","userName":"测试用户","account":"bit-user-001","content":""})",
        "application/json");
    if (emptyComment) {
        const auto body = biteutil::JSON::unserialize(emptyComment->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/comments rejects empty content");
    }

    const auto initialBarrages = client.Get(
        "/videos/barrages?videoId=video-001");
    if (initialBarrages) {
        const auto body = biteutil::JSON::unserialize(initialBarrages->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["barrages"].isArray() &&
                         (*body)["barrages"].empty(),
                     "GET /videos/barrages starts with an empty list");
    }

    const auto sentBarrage = client.Post(
        "/videos/barrages",
        R"({"videoId":"video-001","seconds":8,"text":"第一条弹幕"})",
        "application/json");
    if (sentBarrage) {
        const auto body = biteutil::JSON::unserialize(sentBarrage->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["seconds"].asInt() == 8 &&
                         (*body)["text"].asString() == "第一条弹幕",
                     "POST /videos/barrages returns the saved barrage");
    }

    const auto loadedBarrages = client.Get(
        "/videos/barrages?videoId=video-001");
    if (loadedBarrages) {
        const auto body = biteutil::JSON::unserialize(loadedBarrages->body);
        ok &= expect(body && (*body)["barrages"].size() == 1,
                     "GET /videos/barrages returns saved barrages");
    }

    const auto invalidBarrageSeconds = client.Post(
        "/videos/barrages",
        R"({"videoId":"video-001","seconds":-1,"text":"非法时间"})",
        "application/json");
    if (invalidBarrageSeconds) {
        const auto body = biteutil::JSON::unserialize(
            invalidBarrageSeconds->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/barrages rejects invalid seconds");
    }

    const auto emptyBarrageText = client.Post(
        "/videos/barrages",
        R"({"videoId":"video-001","seconds":8,"text":""})",
        "application/json");
    if (emptyBarrageText) {
        const auto body = biteutil::JSON::unserialize(emptyBarrageText->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /videos/barrages rejects empty text");
    }

    const auto userProfile = client.Get(
        "/users/profile?account=bit-user-001");
    if (userProfile) {
        const auto body = biteutil::JSON::unserialize(userProfile->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["user"]["account"].asString() ==
                             "bit-user-001" &&
                         (*body)["user"]["userName"].asString() == "BIT 用户",
                     "GET /users/profile returns the current user profile");
    }

    const auto updatedProfile = client.Post(
        "/users/profile",
        R"({"account":"bit-user-001","userName":"新昵称","description":"新的简介"})",
        "application/json");
    if (updatedProfile) {
        const auto body = biteutil::JSON::unserialize(updatedProfile->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["user"]["userName"].asString() == "新昵称" &&
                         (*body)["user"]["description"].asString() == "新的简介",
                     "POST /users/profile returns the updated user profile");
    }

    const auto unknownProfile = client.Get(
        "/users/profile?account=missing-user");
    if (unknownProfile) {
        const auto body = biteutil::JSON::unserialize(unknownProfile->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "GET /users/profile reports an unknown user");
    }

    const auto invalidProfileName = client.Post(
        "/users/profile",
        R"({"account":"bit-user-001","userName":"","description":"新的简介"})",
        "application/json");
    if (invalidProfileName) {
        const auto body = biteutil::JSON::unserialize(invalidProfileName->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /users/profile rejects an empty user name");
    }

    httplib::MultipartFormDataItems avatarItems = {
        {"account", "bit-user-001", "", "text/plain"},
        {"avatarFile", "fake-png-content", "avatar.png", "image/png"}};
    const auto avatarUpload = client.Post("/users/avatar", avatarItems);
    if (avatarUpload) {
        const auto body = biteutil::JSON::unserialize(avatarUpload->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         !(*body)["avatarPath"].asString().empty(),
                     "POST /users/avatar uploads an avatar");
    }

    httplib::MultipartFormDataItems badAvatarItems = {
        {"account", "bit-user-001", "", "text/plain"},
        {"avatarFile", "not an image", "avatar.gif", "image/gif"}};
    const auto badAvatar = client.Post("/users/avatar", badAvatarItems);
    if (badAvatar) {
        const auto body = biteutil::JSON::unserialize(badAvatar->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /users/avatar rejects unsupported suffixes");
    }

    httplib::MultipartFormDataItems unknownAvatarItems = {
        {"account", "missing-user", "", "text/plain"},
        {"avatarFile", "fake-png-content", "avatar.png", "image/png"}};
    const auto unknownAvatar = client.Post("/users/avatar", unknownAvatarItems);
    if (unknownAvatar) {
        const auto body = biteutil::JSON::unserialize(unknownAvatar->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /users/avatar reports an unknown user");
    }

    const auto adminReviews = client.Get("/admin/reviews");
    if (adminReviews) {
        const auto body = biteutil::JSON::unserialize(adminReviews->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["reviews"].isArray() &&
                         (*body)["reviews"].size() == 1 &&
                         (*body)["reviews"][0]["status"].asString() == "待审核",
                     "GET /admin/reviews returns review rows");
    }

    const auto reviewAction = client.Post(
        "/admin/reviews/action",
        R"({"videoId":"video-001","status":"审核通过"})",
        "application/json");
    if (reviewAction) {
        const auto body = biteutil::JSON::unserialize(reviewAction->body);
        ok &= expect(body && (*body)["success"].asBool(),
                     "POST /admin/reviews/action updates review status");
    }

    const auto invalidReviewAction = client.Post(
        "/admin/reviews/action",
        R"({"videoId":"video-001","status":"未知状态"})",
        "application/json");
    if (invalidReviewAction) {
        const auto body = biteutil::JSON::unserialize(
            invalidReviewAction->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /admin/reviews/action rejects invalid status");
    }

    const auto adminUsers = client.Get("/admin/users");
    if (adminUsers) {
        const auto body = biteutil::JSON::unserialize(adminUsers->body);
        ok &= expect(body && (*body)["success"].asBool() &&
                         (*body)["users"].isArray() &&
                         (*body)["users"].size() == 2,
                     "GET /admin/users returns user rows");
    }

    const auto userAction = client.Post(
        "/admin/users/action",
        R"({"account":"bit-user-001","action":"disable"})",
        "application/json");
    if (userAction) {
        const auto body = biteutil::JSON::unserialize(userAction->body);
        ok &= expect(body && (*body)["success"].asBool(),
                     "POST /admin/users/action updates a user");
    }

    const auto invalidUserAction = client.Post(
        "/admin/users/action",
        R"({"account":"bit-user-001","action":"unknown"})",
        "application/json");
    if (invalidUserAction) {
        const auto body = biteutil::JSON::unserialize(invalidUserAction->body);
        ok &= expect(body && !(*body)["success"].asBool(),
                     "POST /admin/users/action rejects unsupported actions");
    }

    server.stop();
    serverThread.join();

    FakeRepositories strictStore;
    const biterepo::RepositorySet strictRepositories{
        &strictStore, &strictStore, &strictStore, &strictStore};
    biteserver::HttpServer strictServer(
        strictRepositories, biteserver::ServiceRole::All, "strict_test",
        nullptr, true);
    const int strictPort = strictServer.bindToAnyPort("127.0.0.1");
    ok &= expect(strictPort > 0, "bind strict-auth test server");
    if (strictPort > 0) {
        std::thread strictThread(
            [&strictServer]() { strictServer.listenAfterBind(); });
        httplib::Client strictClient("127.0.0.1", strictPort);
        const auto identityHeaders = [](const std::string& account) {
            return httplib::Headers{
                {"X-Authenticated-Account", account},
                {"X-Gateway-Verified", "1"},
                {"Authorization", "Bearer vod-test-token"},
            };
        };
        const auto userHeaders = identityHeaders("bit-user-001");
        const auto adminHeaders = identityHeaders("admin@bit.com");
        const auto disabledAdminHeaders =
            identityHeaders("disabled-admin@bit.com");

        const auto likeAsOther = strictClient.Post(
            "/videos/like", userHeaders,
            R"({"videoId":"video-001","account":"other-user"})",
            "application/json");
        ok &= expect(likeAsOther && likeAsOther->status == 403,
                     "Alice cannot like with Bob in the body without Redis");
        const auto favoriteAsOther = strictClient.Post(
            "/videos/favorite", userHeaders,
            R"({"videoId":"video-001","account":"other-user"})",
            "application/json");
        ok &= expect(
            favoriteAsOther && favoriteAsOther->status == 403,
            "Alice cannot favorite with Bob in the body without Redis");
        const auto progressAsOther = strictClient.Post(
            "/videos/watch-progress", userHeaders,
            R"({"videoId":"video-001","account":"other-user","seconds":42})",
            "application/json");
        ok &= expect(
            progressAsOther && progressAsOther->status == 403,
            "Alice cannot update Bob watch progress without Redis");
        const auto profileAsOther = strictClient.Post(
            "/users/profile", userHeaders,
            R"({"account":"other-user","userName":"越权","description":""})",
            "application/json");
        ok &= expect(profileAsOther && profileAsOther->status == 403,
                     "Alice cannot update Bob profile without Redis");

        const auto statusAsOther = strictClient.Get(
            "/videos/like-status?videoId=video-001&account=other-user",
            userHeaders);
        ok &= expect(statusAsOther && statusAsOther->status == 403,
                     "Alice cannot query Bob state without Redis");

        const auto publishAsOther = strictClient.Post(
            "/videos", userHeaders,
            R"({"title":"越权发布","account":"other-user","category":"科技","videoFileName":"other.mp4"})",
            "application/json");
        ok &= expect(publishAsOther && publishAsOther->status == 403,
                     "Alice cannot publish metadata as Bob without Redis");

        httplib::MultipartFormDataItems otherAvatar = {
            {"account", "other-user", "", "text/plain"},
            {"avatarFile", "fake-png", "avatar.png", "image/png"}};
        const auto avatarAsOther = strictClient.Post(
            "/users/avatar", userHeaders, otherAvatar);
        ok &= expect(avatarAsOther && avatarAsOther->status == 403,
                     "user A cannot upload user B avatar");

        httplib::MultipartFormDataItems otherUpload = {
            {"metadata",
             R"({"title":"越权上传","account":"other-user","category":"科技"})",
             "", "application/json"},
            {"videoFile", "fake-mp4", "other.mp4", "video/mp4"}};
        const auto uploadAsOther = strictClient.Post(
            "/videos/upload", userHeaders, otherUpload);
        ok &= expect(uploadAsOther && uploadAsOther->status == 403,
                     "Alice cannot upload a video as Bob without Redis");

        const auto missingGatewayIdentity = strictClient.Post(
            "/videos/like",
            R"({"videoId":"video-001","account":"bit-user-001"})",
            "application/json");
        ok &= expect(
            missingGatewayIdentity && missingGatewayIdentity->status == 401,
            "strict mode rejects protected requests without gateway identity");

        const auto normalAdmin = strictClient.Get(
            "/admin/reviews", userHeaders);
        const auto allowedAdmin = strictClient.Get(
            "/admin/reviews", adminHeaders);
        const auto disabledAdmin = strictClient.Get(
            "/admin/reviews", disabledAdminHeaders);
        const auto missingIdentity = strictClient.Get("/admin/reviews");
        ok &= expect(normalAdmin && normalAdmin->status == 403,
                     "ordinary user cannot access admin reviews");
        ok &= expect(allowedAdmin && allowedAdmin->status == 200,
                     "enabled administrator can access admin reviews");
        ok &= expect(disabledAdmin && disabledAdmin->status == 403,
                     "disabled administrator cannot access admin reviews");
        ok &= expect(missingIdentity && missingIdentity->status == 401,
                     "direct protected call without gateway identity is rejected");

        const auto logoutOnce = strictClient.Post(
            "/logout", userHeaders, R"({"account":"bit-user-001"})",
            "application/json");
        const auto logoutTwice = strictClient.Post(
            "/logout", userHeaders, R"({"account":"bit-user-001"})",
            "application/json");
        ok &= expect(logoutOnce && logoutOnce->status == 200 &&
                         logoutTwice && logoutTwice->status == 200,
                     "repeated logout remains successful");

        httplib::MultipartFormDataItems pendingUpload = {
            {"metadata",
             R"({"title":"待审核安全测试","account":"bit-user-001","userName":"伪造昵称","category":"科技","videoFileName":"pending.mp4"})",
             "", "application/json"},
            {"videoFile", "fake-mp4", "pending.mp4", "video/mp4"}};
        const auto created = strictClient.Post(
            "/videos/upload", userHeaders, pendingUpload);
        std::string pendingId;
        if (created) {
            const auto body = biteutil::JSON::unserialize(created->body);
            pendingId = body ? (*body)["video"]["id"].asString() : "";
            ok &= expect(created->status == 200 && body &&
                             (*body)["success"].asBool() && !pendingId.empty(),
                         "createVideo returns its pending video");
        }
        if (!pendingId.empty()) {
            const auto publicList = strictClient.Get("/videos");
            const auto detail = strictClient.Get(
                ("/videos/detail?id=" + pendingId).c_str());
            const auto playUrl = strictClient.Get(
                ("/videos/play-url?videoId=" + pendingId).c_str());
            const std::string interactionBody = "{\"videoId\":\"" +
                pendingId + "\",\"account\":\"bit-user-001\"}";
            const auto likePending = strictClient.Post(
                "/videos/like", userHeaders, interactionBody,
                "application/json");
            const auto favoritePending = strictClient.Post(
                "/videos/favorite", userHeaders, interactionBody,
                "application/json");
            const auto commentPending = strictClient.Post(
                "/videos/comments", userHeaders,
                ("{\"videoId\":\"" + pendingId +
                 "\",\"account\":\"bit-user-001\","
                 "\"userName\":\"伪造昵称\",\"content\":\"test\"}"),
                "application/json");
            const auto ownerVideos = strictClient.Get(
                "/users/videos?account=bit-user-001", userHeaders);

            const auto listBody = publicList
                ? biteutil::JSON::unserialize(publicList->body) : std::nullopt;
            const auto detailBody = detail
                ? biteutil::JSON::unserialize(detail->body) : std::nullopt;
            const auto playBody = playUrl
                ? biteutil::JSON::unserialize(playUrl->body) : std::nullopt;
            const auto likeBody = likePending
                ? biteutil::JSON::unserialize(likePending->body) : std::nullopt;
            const auto favoriteBody = favoritePending
                ? biteutil::JSON::unserialize(favoritePending->body) : std::nullopt;
            const auto commentBody = commentPending
                ? biteutil::JSON::unserialize(commentPending->body) : std::nullopt;
            const auto ownerBody = ownerVideos
                ? biteutil::JSON::unserialize(ownerVideos->body) : std::nullopt;

            bool absentFromList = listBody && listBody->isArray();
            if (absentFromList) {
                for (const auto& item : *listBody) {
                    absentFromList &= item["id"].asString() != pendingId;
                }
            }
            bool presentForOwner = false;
            if (ownerBody && (*ownerBody)["videos"].isArray()) {
                for (const auto& item : (*ownerBody)["videos"]) {
                    presentForOwner |= item["id"].asString() == pendingId;
                }
            }
            ok &= expect(absentFromList,
                         "pending video is absent from public list");
            ok &= expect(detailBody && !(*detailBody)["success"].asBool() &&
                             playBody && !(*playBody)["success"].asBool(),
                         "pending video has no public detail or play URL");
            ok &= expect(likeBody && !(*likeBody)["success"].asBool() &&
                             favoriteBody && !(*favoriteBody)["success"].asBool() &&
                             commentBody && !(*commentBody)["success"].asBool(),
                         "pending video rejects interactions");
            ok &= expect(presentForOwner,
                         "owner can see own pending video in /users/videos");
        }
        strictServer.stop();
        strictThread.join();
    }
    std::filesystem::remove_all("uploads");
    return ok ? 0 : 1;
}
