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

private:
    bool liked_ = false;
    int likeCount_ = 256;
    int watchSeconds_ = 0;
    bool favorited_ = false;
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

    server.stop();
    serverThread.join();
    return ok ? 0 : 1;
}
