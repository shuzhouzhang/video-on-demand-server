#include "../../server/svc_transcode/source/svc_worker.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <optional>
#include <string>

#include <sys/stat.h>

namespace {

class FakeRepository final : public svc_transcode::ITranscodeRepository {
  public:
    svc_transcode::TranscodeJob job{"transcode-video-001",
                                    "video-001",
                                    "alice",
                                    "uploads/source.mp4",
                                    "uploads/transcoded/video-001.mp4",
                                    "PENDING",
                                    0,
                                    2,
                                    "",
                                    ""};
    bool available = true;
    bool succeeded = false;
    bool failed = false;
    bool retrying = false;

    bool enqueueForVideo(const std::string &, const std::string &,
                         const std::string &, svc_transcode::TranscodeJob &,
                         std::string &) override {
        return false;
    }
    bool findByVideoId(const std::string &, const std::string &,
                       std::optional<svc_transcode::TranscodeJob> &,
                       std::string &) override {
        return false;
    }
    bool retry(const std::string &, const std::string &, bool &,
               std::string &) override {
        return false;
    }
    bool recoverExpired(std::string &) override { return true; }
    bool claimNext(const std::string &token, int,
                   std::optional<svc_transcode::TranscodeJob> &claimed,
                   std::string &) override {
        if (!available) {
            claimed.reset();
            return true;
        }
        available = false;
        job.status = "RUNNING";
        ++job.attempts;
        job.leaseToken = token;
        claimed = job;
        return true;
    }
    bool renewLease(const std::string &, const std::string &token, int,
                    bool &renewed, std::string &) override {
        renewed = token == job.leaseToken;
        return true;
    }
    bool markSucceeded(const svc_transcode::TranscodeJob &,
                       const std::string &token, bool &updated,
                       std::string &) override {
        updated = token == job.leaseToken;
        succeeded = updated;
        return true;
    }
    bool markFailed(const svc_transcode::TranscodeJob &claimed,
                    const std::string &token, const std::string &, int,
                    bool &willRetry, std::string &) override {
        failed = token == job.leaseToken;
        willRetry = failed && claimed.attempts < claimed.maxAttempts;
        retrying = willRetry;
        return true;
    }
};

class FakeRunner final : public svc_transcode::ITranscodeRunner {
  public:
    bool result = true;
    bool called = false;
    bool heartbeatCalled = false;

    bool run(svc_transcode::TranscodeJob &,
             const svc_transcode::LeaseHeartbeat &heartbeat,
             std::string &error) override {
        called = true;
        std::string heartbeatError;
        heartbeatCalled = heartbeat(heartbeatError);
        if (!heartbeatCalled) {
            error = heartbeatError;
            return false;
        }
        if (!result)
            error = "synthetic ffmpeg failure";
        return result;
    }
};

bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool successCase() {
    FakeRepository repository;
    FakeRunner runner;
    biteconfig::TranscodeSettings settings;
    svc_transcode::SvcWorker worker(repository, runner, settings);
    bool processed = false;
    std::string error;
    return expect(worker.processOne(processed, error),
                  "success is processed") &&
           expect(processed, "job was claimed") &&
           expect(runner.called && runner.heartbeatCalled,
                  "runner renews lease") &&
           expect(repository.succeeded, "success is persisted") &&
           expect(!repository.failed, "success is not marked failed");
}

bool retryCase() {
    FakeRepository repository;
    FakeRunner runner;
    runner.result = false;
    biteconfig::TranscodeSettings settings;
    svc_transcode::SvcWorker worker(repository, runner, settings);
    bool processed = false;
    std::string error;
    return expect(worker.processOne(processed, error),
                  "failure is persisted") &&
           expect(processed, "failed job was claimed") &&
           expect(repository.failed, "failure is recorded") &&
           expect(repository.retrying, "failure below limit is retried") &&
           expect(!repository.succeeded, "failure is not marked successful");
}

bool emptyQueueCase() {
    FakeRepository repository;
    repository.available = false;
    FakeRunner runner;
    biteconfig::TranscodeSettings settings;
    svc_transcode::SvcWorker worker(repository, runner, settings);
    bool processed = true;
    std::string error;
    return expect(worker.processOne(processed, error),
                  "empty queue is valid") &&
           expect(!processed, "empty queue reports no work") &&
           expect(!runner.called, "runner is not called without a job");
}

bool migrationContractCase() {
    std::ifstream input("../../migrations/014_create_transcode_jobs.sql");
    const std::string sql((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
    return expect(!sql.empty(), "transcode migration exists") &&
           expect(sql.find("transcode_status") != std::string::npos,
                  "migration adds video transcode state") &&
           expect(sql.find("lease_token") != std::string::npos &&
                      sql.find("next_attempt_at") != std::string::npos,
                  "migration persists lease and retry fields") &&
           expect(sql.find("uk_transcode_jobs_video_id") != std::string::npos,
                  "migration enforces one job per video");
}

bool runnerBoundaryCase() {
    namespace fs = std::filesystem;
    const fs::path base = "/tmp/vod-transcode-runner-test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "uploads/transcoded", ec);
    {
        std::ofstream source(base / "uploads/source.mp4", std::ios::binary);
        source << "synthetic media";
    }
    const fs::path executable = base / "fake-ffmpeg";
    {
        std::ofstream script(executable);
        script
            << "#!/bin/sh\nlast=''\nfor arg in \"$@\"; do last=\"$arg\"; done\n"
               "cp \"$7\" \"$last\"\n";
    }
    ::chmod(executable.c_str(), 0700);

    biteconfig::TranscodeSettings settings;
    settings.ffmpegPath = executable.string();
    settings.uploadRoot = (base / "uploads").string();
    settings.outputRoot = (base / "uploads/transcoded").string();
    svc_transcode::FfmpegRunner runner(settings);
    svc_transcode::TranscodeJob job{
        "transcode-video-002",
        "video-002",
        "alice",
        (base / "uploads/source.mp4").string(),
        (base / "uploads/transcoded/video-002.mp4").string(),
        "RUNNING",
        1,
        2,
        "",
        "lease"};
    std::string error;
    const bool ran = runner.run(
        job, [](std::string &) { return true; }, error);
    std::ifstream output(job.outputPath, std::ios::binary);
    const std::string body((std::istreambuf_iterator<char>(output)),
                           std::istreambuf_iterator<char>());
    const bool success =
        expect(ran, "runner executes argv-based process") &&
        expect(body == "synthetic media", "runner atomically publishes output");

    job.outputPath = (base / "escaped.mp4").string();
    error.clear();
    const bool rejected = !runner.run(
        job, [](std::string &) { return true; }, error);
    fs::remove_all(base, ec);
    return success &&
           expect(rejected && error.find("outside") != std::string::npos,
                  "runner rejects output outside configured root");
}

} // namespace

int main() {
    if (!successCase() || !retryCase() || !emptyQueueCase() ||
        !migrationContractCase())
        return 1;
    if (!runnerBoundaryCase())
        return 1;
    std::cout << "transcode tests passed\n";
    return 0;
}
