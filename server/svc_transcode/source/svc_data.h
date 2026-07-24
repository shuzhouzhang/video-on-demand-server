#pragma once

#include "../../database/database.h"

#include <optional>
#include <string>

namespace svc_transcode {

struct TranscodeJob {
    std::string jobId;
    std::string videoId;
    std::string ownerAccount;
    std::string inputPath;
    std::string outputPath;
    std::string status;
    unsigned int attempts = 0;
    unsigned int maxAttempts = 0;
    std::string errorMessage;
    std::string leaseToken;
};

class ITranscodeRepository {
public:
    virtual ~ITranscodeRepository() = default;

    virtual bool enqueueForVideo(const std::string& videoId,
                                 const std::string& ownerAccount,
                                 TranscodeJob& job,
                                 std::string& error) = 0;
    virtual bool findByVideoId(const std::string& videoId,
                               const std::string& ownerAccount,
                               std::optional<TranscodeJob>& job,
                               std::string& error) = 0;
    virtual bool retry(const std::string& videoId,
                       const std::string& ownerAccount,
                       bool& updated,
                       std::string& error) = 0;
    virtual bool recoverExpired(std::string& error) = 0;
    virtual bool claimNext(const std::string& leaseToken,
                           int leaseSeconds,
                           std::optional<TranscodeJob>& job,
                           std::string& error) = 0;
    virtual bool renewLease(const std::string& jobId,
                            const std::string& leaseToken,
                            int leaseSeconds,
                            bool& renewed,
                            std::string& error) = 0;
    virtual bool markSucceeded(const TranscodeJob& job,
                               const std::string& leaseToken,
                               bool& updated,
                               std::string& error) = 0;
    virtual bool markFailed(const TranscodeJob& job,
                            const std::string& leaseToken,
                            const std::string& reason,
                            int retryDelaySeconds,
                            bool& willRetry,
                            std::string& error) = 0;
};

class MySqlTranscodeRepository final : public ITranscodeRepository {
public:
    MySqlTranscodeRepository(bitedb::Database& database,
                             unsigned int defaultMaxAttempts);

    bool enqueueForVideo(const std::string& videoId,
                         const std::string& ownerAccount,
                         TranscodeJob& job,
                         std::string& error) override;
    bool findByVideoId(const std::string& videoId,
                       const std::string& ownerAccount,
                       std::optional<TranscodeJob>& job,
                       std::string& error) override;
    bool retry(const std::string& videoId,
               const std::string& ownerAccount,
               bool& updated,
               std::string& error) override;
    bool recoverExpired(std::string& error) override;
    bool claimNext(const std::string& leaseToken,
                   int leaseSeconds,
                   std::optional<TranscodeJob>& job,
                   std::string& error) override;
    bool renewLease(const std::string& jobId,
                    const std::string& leaseToken,
                    int leaseSeconds,
                    bool& renewed,
                    std::string& error) override;
    bool markSucceeded(const TranscodeJob& job,
                       const std::string& leaseToken,
                       bool& updated,
                       std::string& error) override;
    bool markFailed(const TranscodeJob& job,
                    const std::string& leaseToken,
                    const std::string& reason,
                    int retryDelaySeconds,
                    bool& willRetry,
                    std::string& error) override;

private:
    bool readJob(const std::string& whereClause,
                 std::optional<TranscodeJob>& job,
                 std::string& error);

    bitedb::Database& database_;
    unsigned int defaultMaxAttempts_;
};

}  // namespace svc_transcode
