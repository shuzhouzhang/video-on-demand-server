#include "svc_worker.h"

#include "../../common/bitelog.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <system_error>

#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace svc_transcode {
namespace {

namespace fs = std::filesystem;

bool isInside(const fs::path& child, const fs::path& parent) {
    auto childIt = child.begin();
    auto parentIt = parent.begin();
    for (; parentIt != parent.end(); ++parentIt, ++childIt) {
        if (childIt == child.end() || *childIt != *parentIt) return false;
    }
    return true;
}

bool resolveSafePath(const std::string& raw,
                     const std::string& root,
                     bool mustExist,
                     fs::path& resolved,
                     std::string& error) {
    std::error_code ec;
    const fs::path rootPath = fs::weakly_canonical(fs::absolute(root), ec);
    if (ec) {
        error = "cannot resolve transcode root";
        return false;
    }
    resolved = fs::weakly_canonical(fs::absolute(raw), ec);
    if (ec || !isInside(resolved, rootPath)) {
        error = "transcode path is outside the configured root";
        return false;
    }
    if (mustExist && !fs::is_regular_file(resolved, ec)) {
        error = "transcode input file does not exist";
        return false;
    }
    return true;
}

}  // namespace

FfmpegRunner::FfmpegRunner(biteconfig::TranscodeSettings settings)
    : settings_(std::move(settings)) {}

bool FfmpegRunner::run(const TranscodeJob& job,
                       const LeaseHeartbeat& heartbeat,
                       std::string& error) {
    fs::path input;
    fs::path output;
    if (!resolveSafePath(job.inputPath, settings_.uploadRoot, true, input,
                         error) ||
        !resolveSafePath(job.outputPath, settings_.outputRoot, false, output,
                         error)) {
        return false;
    }

    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    if (ec) {
        error = "cannot create transcode output directory";
        return false;
    }
    const fs::path temporary = output.string() + "." + job.jobId + ".tmp.mp4";
    fs::remove(temporary, ec);

    const std::string executable = settings_.ffmpegPath;
    const std::string inputString = input.string();
    const std::string temporaryString = temporary.string();
    const std::vector<std::string> arguments = {
        executable, "-nostdin", "-hide_banner", "-loglevel", "error",
        "-y", "-i", inputString, "-map_metadata", "-1", "-c:v",
        "libx264", "-preset", "veryfast", "-movflags", "+faststart",
        "-c:a", "aac", "-b:a", "128k", temporaryString};
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        error = "cannot start ffmpeg";
        return false;
    }
    if (pid == 0) {
        ::execvp(executable.c_str(), argv.data());
        ::_exit(127);
    }

    int status = 0;
    auto nextHeartbeat = std::chrono::steady_clock::now() +
        std::chrono::seconds(std::max(1, settings_.leaseSeconds / 3));
    while (true) {
        const pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) break;
        if (result < 0) {
            error = "cannot wait for ffmpeg";
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
            fs::remove(temporary, ec);
            return false;
        }
        if (std::chrono::steady_clock::now() >= nextHeartbeat) {
            std::string heartbeatError;
            if (!heartbeat(heartbeatError)) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, nullptr, 0);
                fs::remove(temporary, ec);
                error = heartbeatError.empty() ? "transcode lease was lost"
                                               : heartbeatError;
                return false;
            }
            nextHeartbeat = std::chrono::steady_clock::now() +
                std::chrono::seconds(std::max(1, settings_.leaseSeconds / 3));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fs::remove(temporary, ec);
        error = WIFEXITED(status)
            ? "ffmpeg exited with code " + std::to_string(WEXITSTATUS(status))
            : "ffmpeg terminated unexpectedly";
        return false;
    }
    fs::rename(temporary, output, ec);
    if (ec) {
        fs::remove(temporary, ec);
        error = "cannot publish transcoded output";
        return false;
    }
    return true;
}

SvcWorker::SvcWorker(ITranscodeRepository& repository,
                     ITranscodeRunner& runner,
                     biteconfig::TranscodeSettings settings)
    : repository_(repository), runner_(runner), settings_(std::move(settings)) {}

SvcWorker::~SvcWorker() { stop(); }

void SvcWorker::start() {
    bool expected = true;
    if (!stopped_.compare_exchange_strong(expected, false)) return;
    const int count = std::max(1, settings_.workerThreads);
    threads_.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        threads_.emplace_back(&SvcWorker::threadEntry, this);
    }
}

void SvcWorker::stop() {
    stopped_.store(true);
    for (auto& thread : threads_) {
        if (thread.joinable()) thread.join();
    }
    threads_.clear();
}

std::string SvcWorker::makeLeaseToken() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "lease-" << ::getpid() << '-' << now << '-' << ++leaseCounter_;
    return out.str();
}

bool SvcWorker::processOne(bool& processed, std::string& error) {
    processed = false;
    const std::string token = makeLeaseToken();
    std::optional<TranscodeJob> job;
    if (!repository_.claimNext(token, settings_.leaseSeconds, job, error)) {
        return false;
    }
    if (!job) return true;
    processed = true;

    const LeaseHeartbeat heartbeat = [this, &job, &token](std::string& out) {
        bool renewed = false;
        if (!repository_.renewLease(job->jobId, token, settings_.leaseSeconds,
                                    renewed, out)) {
            return false;
        }
        if (!renewed) out = "transcode lease was lost";
        return renewed;
    };
    std::string runError;
    if (runner_.run(*job, heartbeat, runError)) {
        bool updated = false;
        if (!repository_.markSucceeded(*job, token, updated, error)) return false;
        if (!updated) {
            error = "transcode completion rejected because lease was lost";
            return false;
        }
        return true;
    }

    bool willRetry = false;
    if (!repository_.markFailed(*job, token, runError,
                                settings_.retryDelaySeconds, willRetry,
                                error)) {
        return false;
    }
    return true;
}

void SvcWorker::threadEntry() {
    while (!stopped_.load()) {
        bool processed = false;
        std::string error;
        if (!processOne(processed, error) && !error.empty()) {
            ERR("transcode worker iteration failed: {}", error);
        }
        if (!processed) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(std::max(10, settings_.pollIntervalMs)));
        }
    }
}

}  // namespace svc_transcode
