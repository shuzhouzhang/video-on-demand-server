#include "../../server/common/auth.h"
#include "../../server/common/redis_session_manager.h"
#include "../../server/database/database.h"
#include "../../server/svc_transcode/source/svc_data.h"
#include "../../server/svc_user/source/user_repository.h"
#include "../../server/svc_video/source/video_repository.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string env(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

std::string envAllowEmpty(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    else std::cout << "[PASS] " << message << '\n';
    return condition;
}

std::optional<unsigned long long> scalarUnsigned(
    bitedb::Database& database,
    const std::string& sql,
    const std::vector<std::string>& parameters,
    std::string& error) {
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database.queryPrepared(sql, parameters, rows, error) || rows.empty() ||
        rows.front().empty() || !rows.front().front()) {
        return std::nullopt;
    }
    try {
        return std::stoull(*rows.front().front());
    } catch (const std::exception&) {
        error = "scalar result is not unsigned";
        return std::nullopt;
    }
}

bitevideo::VideoDraft videoDraft(const std::string& title,
                                 const std::string& source) {
    bitevideo::VideoDraft draft;
    draft.title = title;
    draft.account = "it-alice";
    draft.userName = "Integration Alice";
    draft.category = "测试";
    draft.description = "integration";
    draft.videoFileName = "same-name.mp4";
    draft.playUrl = source;
    return draft;
}

}  // namespace

int main() {
    bool ok = true;
    std::string error;
    biteconfig::DatabaseSettings databaseSettings{
        env("VIDEO_TEST_MYSQL_HOST", "127.0.0.1"),
        static_cast<std::uint16_t>(
            std::stoi(env("VIDEO_TEST_MYSQL_PORT", "3306"))),
        env("VIDEO_TEST_MYSQL_USER", "video_app"),
        envAllowEmpty("VIDEO_TEST_MYSQL_PASSWORD", "video_app_password"),
        env("VIDEO_TEST_MYSQL_DATABASE", "video_on_demand")};
    bitedb::Database database;
    if (!expect(database.connect(databaseSettings, error),
                "connect to integration MySQL")) {
        std::cerr << error << '\n';
        return 1;
    }

    database.executePrepared("DELETE FROM videos WHERE owner_account = ?",
                             {"it-alice"}, error);
    database.executePrepared("DELETE FROM video_likes WHERE account = ?",
                             {"it-alice"}, error);
    database.executePrepared(
        "DELETE FROM video_watch_progress WHERE account = ?", {"it-alice"},
        error);
    // Normalize the seed through the same utf8mb4 prepared path used by the
    // service. This also protects the test from a mysql CLI client charset.
    database.executePrepared(
        "UPDATE videos SET review_status = ?, transcode_status = 'READY', "
        "status = 1 WHERE video_id = ?", {"审核通过", "video-002"}, error);
    if (!database.executePrepared(
            "INSERT INTO users (account, password, user_name, role, status, "
            "description, avatar_path) VALUES (?, ?, ?, '普通用户', '启用', "
            "'', '') ON DUPLICATE KEY UPDATE password = VALUES(password), "
            "user_name = VALUES(user_name), status = '启用'",
            {"it-alice", "integration-password", "Integration Alice"},
            error)) {
        std::cerr << error << '\n';
        return 1;
    }

    biteuser::MySqlUserRepository users(database);
    std::optional<bitevideo::UserProfile> profile;
    ok &= expect(users.passwordLogin("it-alice", "integration-password",
                                     profile, error) && profile,
                 "login verifies MySQL credentials");

    biteconfig::RedisSettings redisSettings;
    redisSettings.enabled = true;
    redisSettings.host = env("VIDEO_TEST_REDIS_HOST", "127.0.0.1");
    redisSettings.port = static_cast<std::uint16_t>(
        std::stoi(env("VIDEO_TEST_REDIS_PORT", "6379")));
    redisSettings.password = envAllowEmpty("VIDEO_TEST_REDIS_PASSWORD", "");
    redisSettings.sessionTtlSeconds = 120;
    bitesession::RedisSessionManager sessions(redisSettings);
    ok &= expect(sessions.connect(error), "connect auth integration Redis");
    std::string token;
    ok &= expect(sessions.createToken("it-alice", token, error),
                 "login account receives Redis token");
    const auto gateway = biteauth::authenticateGatewayRequest(
        true, true, "Bearer " + token,
        [&sessions](const std::string& value, std::string& lookupError) {
            return sessions.accountForToken(value, lookupError);
        });
    ok &= expect(gateway.status == biteauth::GatewayAuthStatus::Allowed &&
                     gateway.account == "it-alice",
                 "Gateway resolves token to Alice");
    httplib::Request forged;
    forged.headers.emplace(biteauth::GATEWAY_VERIFIED_HEADER, "1");
    forged.headers.emplace(biteauth::AUTHENTICATED_ACCOUNT_HEADER, "it-alice");
    ok &= expect(biteauth::bindAuthenticatedAccount(
                     forged, "bob", true).status ==
                     biteauth::IdentityStatus::Forbidden,
                 "Alice cannot claim Bob downstream");
    ok &= expect(sessions.deleteToken(token, error) &&
                     !sessions.accountForToken(token, error) && error.empty(),
                 "logout invalidates Redis token");

    bitevideo::MySqlVideoRepository videos(database);
    const auto baselineLikes = scalarUnsigned(
        database, "SELECT like_count FROM videos WHERE video_id = ?",
        {"video-002"}, error);
    database.executePrepared(
        "DELETE FROM video_likes WHERE video_id = ? AND account = ?",
        {"video-002", "it-alice"}, error);
    std::atomic<bool> likesOk{true};
    std::mutex likeErrorMutex;
    std::string likeError;
    std::vector<std::thread> likeThreads;
    for (int index = 0; index < 20; ++index) {
        likeThreads.emplace_back([&]() {
            std::optional<bitevideo::LikeStatus> status;
            std::string threadError;
            if (!videos.setLiked("video-002", "it-alice", true, status,
                                 threadError) || !status || !status->liked) {
                likesOk.store(false);
                std::lock_guard<std::mutex> lock(likeErrorMutex);
                if (likeError.empty()) likeError = threadError;
            }
        });
    }
    for (auto& thread : likeThreads) thread.join();
    const auto likedCount = scalarUnsigned(
        database, "SELECT like_count FROM videos WHERE video_id = ?",
        {"video-002"}, error);
    ok &= expect(baselineLikes && likedCount && likesOk.load() &&
                     *likedCount == *baselineLikes + 1,
                 "concurrent duplicate likes increment once");
    if (!likesOk.load() || !baselineLikes || !likedCount ||
        (baselineLikes && likedCount &&
         *likedCount != *baselineLikes + 1)) {
        std::cerr << "like diagnostics: baseline="
                  << (baselineLikes ? std::to_string(*baselineLikes) : "none")
                  << " current="
                  << (likedCount ? std::to_string(*likedCount) : "none")
                  << " error=" << likeError << '\n';
    }
    std::optional<bitevideo::LikeStatus> likeStatus;
    videos.setLiked("video-002", "it-alice", false, likeStatus, error);
    videos.setLiked("video-002", "it-alice", false, likeStatus, error);
    const auto unlikedCount = scalarUnsigned(
        database, "SELECT like_count FROM videos WHERE video_id = ?",
        {"video-002"}, error);
    ok &= expect(unlikedCount && baselineLikes &&
                     *unlikedCount == *baselineLikes,
                 "repeated unlike decrements once");

    std::optional<bitevideo::WatchProgress> progress;
    const bool firstProgress = videos.saveWatchProgress(
        "video-002", "it-alice", 10, progress, error);
    const bool secondProgress = firstProgress && videos.saveWatchProgress(
        "video-002", "it-alice", 42, progress, error);
    ok &= expect(firstProgress && secondProgress && progress &&
                     progress->seconds == 42,
                 "later watch progress overwrites earlier value");
    if (!firstProgress || !secondProgress || !progress ||
        progress->seconds != 42) {
        std::cerr << "progress diagnostics: first=" << firstProgress
                  << " second=" << secondProgress << " error=" << error
                  << '\n';
    }
    const auto progressRows = scalarUnsigned(
        database,
        "SELECT COUNT(*) FROM video_watch_progress WHERE video_id = ? "
        "AND account = ?", {"video-002", "it-alice"}, error);
    ok &= expect(progressRows && *progressRows == 1,
                 "watch progress keeps one row per video and account");

    std::mutex idsMutex;
    std::set<std::string> videoIds;
    std::atomic<bool> createOk{true};
    std::vector<std::thread> createThreads;
    for (int index = 0; index < 20; ++index) {
        createThreads.emplace_back([&, index]() {
            auto draft = videoDraft("concurrent-" + std::to_string(index),
                                    "https://example.invalid/video.mp4");
            std::optional<bitevideo::Video> created;
            std::string threadError;
            if (!videos.createVideo(draft, created, threadError) || !created) {
                createOk.store(false);
                return;
            }
            std::lock_guard<std::mutex> lock(idsMutex);
            videoIds.insert(created->id);
        });
    }
    for (auto& thread : createThreads) thread.join();
    ok &= expect(createOk.load() && videoIds.size() == 20,
                 "concurrent video creation produces unique video_id values");

    svc_transcode::MySqlTranscodeRepository transcodes(database, 3);
    std::optional<bitevideo::Video> leaseVideo;
    ok &= expect(videos.createVideo(
                     videoDraft("lease-test", "uploads/lease-source.mp4"),
                     leaseVideo, error) && leaseVideo,
                 "create video with transcode job");
    std::optional<svc_transcode::TranscodeJob> firstClaim;
    std::optional<svc_transcode::TranscodeJob> secondClaim;
    std::thread workerA([&]() {
        std::string threadError;
        if (!transcodes.claimNext("lease-worker-a", 60, firstClaim,
                                  threadError)) createOk.store(false);
    });
    std::thread workerB([&]() {
        std::string threadError;
        if (!transcodes.claimNext("lease-worker-b", 60, secondClaim,
                                  threadError)) createOk.store(false);
    });
    workerA.join();
    workerB.join();
    ok &= expect(createOk.load() &&
                     static_cast<bool>(firstClaim) !=
                         static_cast<bool>(secondClaim),
                 "two workers cannot claim the same job");
    auto claimed = firstClaim ? firstClaim : secondClaim;
    const std::string oldLease = firstClaim ? "lease-worker-a" :
                                             "lease-worker-b";
    if (claimed) {
        database.executePrepared(
            "UPDATE transcode_jobs SET lease_until = DATE_SUB(NOW(), "
            "INTERVAL 1 SECOND) WHERE job_id = ?", {claimed->jobId}, error);
        ok &= expect(transcodes.recoverExpired(error),
                     "expired lease is recovered");
        std::optional<svc_transcode::TranscodeJob> recovered;
        transcodes.findByVideoId(claimed->videoId, "it-alice", recovered,
                                 error);
        ok &= expect(recovered && recovered->status == "PENDING",
                     "expired job returns to pending");
        std::optional<svc_transcode::TranscodeJob> newClaim;
        transcodes.claimNext("lease-worker-new", 60, newClaim, error);
        bool updated = true;
        transcodes.markSucceeded(*claimed, oldLease, updated, error);
        ok &= expect(!updated,
                     "worker that lost its lease cannot mark succeeded");
        if (newClaim) {
            transcodes.markSucceeded(*newClaim, "lease-worker-new", updated,
                                     error);
            ok &= expect(updated, "current lease can mark job succeeded");
        } else {
            ok &= expect(false, "recovered job can be claimed again");
        }
    }

    std::optional<bitevideo::Video> retryVideo;
    ok &= expect(videos.createVideo(
                     videoDraft("retry-test", "uploads/retry-source.mp4"),
                     retryVideo, error) && retryVideo,
                 "create retry-limit transcode job");
    std::optional<svc_transcode::TranscodeJob> retryJob;
    bool finalWillRetry = true;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        retryJob.reset();
        const std::string lease = "retry-lease-" + std::to_string(attempt);
        if (!transcodes.claimNext(lease, 60, retryJob, error) || !retryJob) {
            createOk.store(false);
            break;
        }
        if (!transcodes.markFailed(*retryJob, lease, "integration failure", 0,
                                   finalWillRetry, error)) {
            createOk.store(false);
            break;
        }
    }
    std::optional<svc_transcode::TranscodeJob> failedJob;
    if (retryVideo) {
        transcodes.findByVideoId(retryVideo->id, "it-alice", failedJob, error);
    }
    ok &= expect(createOk.load() && failedJob &&
                     failedJob->status == "FAILED" && !finalWillRetry,
                 "transcode reaches FAILED at max attempts");

    sessions.deleteToken(token, error);
    database.executePrepared("DELETE FROM videos WHERE owner_account = ?",
                             {"it-alice"}, error);
    database.executePrepared("DELETE FROM video_likes WHERE account = ?",
                             {"it-alice"}, error);
    database.executePrepared(
        "DELETE FROM video_watch_progress WHERE account = ?", {"it-alice"},
        error);
    database.executePrepared("DELETE FROM users WHERE account = ?",
                             {"it-alice"}, error);
    return ok ? 0 : 1;
}
