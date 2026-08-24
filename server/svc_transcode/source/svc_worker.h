#pragma once

#include "svc_data.h"
#include "../../common/config.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace svc_transcode {

using LeaseHeartbeat = std::function<bool(std::string& error)>;

class ITranscodeRunner {
public:
    virtual ~ITranscodeRunner() = default;
    virtual bool run(const TranscodeJob& job,
                     const LeaseHeartbeat& heartbeat,
                     std::string& error) = 0;
};

class FfmpegRunner final : public ITranscodeRunner {
public:
    explicit FfmpegRunner(biteconfig::TranscodeSettings settings);
    bool run(const TranscodeJob& job,
             const LeaseHeartbeat& heartbeat,
             std::string& error) override;

private:
    biteconfig::TranscodeSettings settings_;
};

class SvcWorker {
public:
    SvcWorker(ITranscodeRepository& repository,
              ITranscodeRunner& runner,
              biteconfig::TranscodeSettings settings);
    ~SvcWorker();

    SvcWorker(const SvcWorker&) = delete;
    SvcWorker& operator=(const SvcWorker&) = delete;

    void start();
    void stop();
    bool processOne(bool& processed, std::string& error);

private:
    void threadEntry();
    std::string makeLeaseToken();

    ITranscodeRepository& repository_;
    ITranscodeRunner& runner_;
    biteconfig::TranscodeSettings settings_;
    std::atomic<bool> stopped_{true};
    std::atomic<unsigned long long> leaseCounter_{0};
    std::vector<std::thread> threads_;
};

}  // namespace svc_transcode
