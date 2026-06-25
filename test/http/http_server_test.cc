#include "../../source/http_server.h"
#include "../../source/util.h"

#include <iostream>
#include <string>
#include <thread>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    std::cout << "[PASS] " << message << '\n';
    return true;
}

class FakeVideoStore : public bitevideo::VideoStore {
public:
    bool list(std::vector<bitevideo::Video>& videos,
              std::string& error) override {
        error.clear();
        videos = {{"video-001", "测试视频", "测试用户", "6-23", 558,
                   "36000", "256", "科技", {"编程开发", "软件工具"},
                   "HTTP测试数据"}};
        return true;
    }

    bool findById(const std::string& videoId,
                  std::optional<bitevideo::Video>& video,
                  std::string& error) override {
        error.clear();
        if (videoId == "video-001") {
            video = bitevideo::Video{
                "video-001", "测试视频", "测试用户", "6-23", 558,
                "36000", "256", "科技", {"编程开发", "软件工具"},
                "HTTP测试数据"};
        } else {
            video.reset();
        }
        return true;
    }

    bool search(const std::string& keyword,
                std::vector<bitevideo::Video>& videos,
                std::string& error) override {
        error.clear();
        videos.clear();
        if (keyword == "测试" || keyword == "科技") {
            list(videos, error);
        }
        return true;
    }

    bool playUrl(const std::string& videoId,
                 std::optional<std::string>& url,
                 std::string& error) override {
        error.clear();
        if (videoId == "video-001") {
            url = "D:/video-on-demand-client/test.mp4";
        } else {
            url.reset();
        }
        return true;
    }

    bool likeStatus(const std::string& videoId,
                    const std::string&,
                    std::optional<bitevideo::LikeStatus>& status,
                    std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            status.reset();
        } else {
            status = bitevideo::LikeStatus{liked_, std::to_string(likeCount_)};
        }
        return true;
    }

    bool setLiked(const std::string& videoId,
                  const std::string& account,
                  bool shouldLike,
                  std::optional<bitevideo::LikeStatus>& status,
                  std::string& error) override {
        if (!likeStatus(videoId, account, status, error) || !status) {
            return true;
        }
        if (shouldLike && !liked_) {
            liked_ = true;
            ++likeCount_;
        } else if (!shouldLike && liked_) {
            liked_ = false;
            --likeCount_;
        }
        status = bitevideo::LikeStatus{liked_, std::to_string(likeCount_)};
        return true;
    }

    bool watchProgress(const std::string& videoId,
                       const std::string&,
                       std::optional<bitevideo::WatchProgress>& progress,
                       std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            progress.reset();
        } else {
            progress = bitevideo::WatchProgress{watchSeconds_};
        }
        return true;
    }

    bool saveWatchProgress(const std::string& videoId,
                           const std::string& account,
                           int seconds,
                           std::optional<bitevideo::WatchProgress>& progress,
                           std::string& error) override {
        if (!watchProgress(videoId, account, progress, error) || !progress) {
            return true;
        }
        watchSeconds_ = seconds;
        progress = bitevideo::WatchProgress{watchSeconds_};
        return true;
    }

    bool favoriteStatus(const std::string& videoId,
                        const std::string&,
                        std::optional<bitevideo::FavoriteStatus>& status,
                        std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            status.reset();
        } else {
            status = bitevideo::FavoriteStatus{favorited_};
        }
        return true;
    }

    bool setFavorited(const std::string& videoId,
                      const std::string& account,
                      bool shouldFavorite,
                      std::optional<bitevideo::FavoriteStatus>& status,
                      std::string& error) override {
        if (!favoriteStatus(videoId, account, status, error) || !status) {
            return true;
        }
        favorited_ = shouldFavorite;
        status = bitevideo::FavoriteStatus{favorited_};
        return true;
    }

    bool favoriteVideos(const std::string&,
                        std::vector<bitevideo::Video>& videos,
                        std::string& error) override {
        error.clear();
        videos.clear();
        if (favorited_) {
            list(videos, error);
        }
        return true;
    }

    bool comments(const std::string& videoId,
                  std::optional<std::vector<bitevideo::VideoComment>>& comments,
                  std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            comments.reset();
        } else {
            comments = comments_;
        }
        return true;
    }

    bool addComment(const std::string& videoId,
                    const std::string& userName,
                    const std::string& account,
                    const std::string& content,
                    std::optional<bitevideo::VideoComment>& comment,
                    std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            comment.reset();
            return true;
        }
        bitevideo::VideoComment saved{
            "comment-001", videoId, userName, account, content,
            "2026-06-25 11:40"};
        comments_.insert(comments_.begin(), saved);
        comment = saved;
        return true;
    }

    bool barrages(const std::string& videoId,
                  std::optional<std::vector<bitevideo::VideoBarrage>>& barrages,
                  std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            barrages.reset();
        } else {
            barrages = barrages_;
        }
        return true;
    }

    bool addBarrage(const std::string& videoId,
                    int seconds,
                    const std::string& text,
                    std::optional<bitevideo::VideoBarrage>& barrage,
                    std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            barrage.reset();
            return true;
        }
        bitevideo::VideoBarrage saved{seconds, text};
        barrages_.push_back(saved);
        barrage = saved;
        return true;
    }

private:
    bool liked_ = false;
    int likeCount_ = 256;
    int watchSeconds_ = 0;
    bool favorited_ = false;
    std::vector<bitevideo::VideoComment> comments_;
    std::vector<bitevideo::VideoBarrage> barrages_;
};

}  // namespace

int main() {
    bool ok = true;
    FakeVideoStore videoStore;
    biteserver::HttpServer server(videoStore);
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

    server.stop();
    serverThread.join();
    return ok ? 0 : 1;
}
