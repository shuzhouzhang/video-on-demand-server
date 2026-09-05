#include "svc_data.h"

#include "../../common/outbox.h"
#include "../../common/util.h"

#ifdef VOD_ENABLE_REFERENCE_RUNTIME
#include "message.pb.h"
#endif

#include <algorithm>
#include <vector>

namespace svc_transcode {
namespace {

std::string valueOrEmpty(const std::optional<std::string>& value) {
    return value.value_or("");
}

bool parseJob(const bitedb::Database::QueryRow& row,
              TranscodeJob& job,
              std::string& error) {
    if (row.size() != 10) {
        error = "transcode job query returned unexpected fields";
        return false;
    }
    job.jobId = valueOrEmpty(row[0]);
    job.videoId = valueOrEmpty(row[1]);
    job.ownerAccount = valueOrEmpty(row[2]);
    job.inputPath = valueOrEmpty(row[3]);
    job.outputPath = valueOrEmpty(row[4]);
    job.status = valueOrEmpty(row[5]);
    job.errorMessage = valueOrEmpty(row[8]);
    job.leaseToken = valueOrEmpty(row[9]);
    try {
        job.attempts = static_cast<unsigned int>(
            std::stoul(valueOrEmpty(row[6])));
        job.maxAttempts = static_cast<unsigned int>(
            std::stoul(valueOrEmpty(row[7])));
    } catch (const std::exception&) {
        error = "transcode job attempt count is invalid";
        return false;
    }
    return true;
}

constexpr const char* JOB_SELECT =
    "SELECT job_id, video_id, owner_account, input_path, output_path, "
    "status, CAST(attempts AS CHAR), CAST(max_attempts AS CHAR), "
    "error_message, lease_token FROM transcode_jobs WHERE ";

}  // namespace

MySqlTranscodeRepository::MySqlTranscodeRepository(
    bitedb::Database& database, unsigned int defaultMaxAttempts,
    biteevent::MySqlOutboxRepository* outbox)
    : database_(database),
      defaultMaxAttempts_(std::max(1U, defaultMaxAttempts)),
      outbox_(outbox) {}

bool MySqlTranscodeRepository::readJob(
    const std::string& whereClause,
    std::optional<TranscodeJob>& job,
    std::string& error) {
    job.reset();
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(std::string(JOB_SELECT) + whereClause + " LIMIT 1",
                         rows, error)) {
        return false;
    }
    if (rows.empty()) return true;
    TranscodeJob value;
    if (!parseJob(rows.front(), value, error)) return false;
    job = std::move(value);
    return true;
}

bool MySqlTranscodeRepository::enqueueForVideo(
    const std::string& videoId,
    const std::string& ownerAccount,
    const std::string& requestId,
    TranscodeJob& job,
    std::string& error) {
#ifndef VOD_ENABLE_REFERENCE_RUNTIME
    (void)requestId;
#endif
    std::string video;
    std::string owner;
    if (!database_.escape(videoId, video, error) ||
        !database_.escape(ownerAccount, owner, error)) {
        return false;
    }
    std::optional<TranscodeJob> existing;
    if (!readJob("video_id = '" + video + "' AND owner_account = '" + owner +
                     "'",
                 existing, error)) {
        return false;
    }
    if (existing) {
        job = *existing;
        return true;
    }
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(
            "SELECT owner_account, play_url FROM videos WHERE video_id = '" +
                video + "' LIMIT 1",
            rows, error)) {
        return false;
    }
    if (rows.empty() || rows.front().size() != 2 ||
        valueOrEmpty(rows.front()[0]) != ownerAccount) {
        error = "video not found or not owned by current account";
        return false;
    }
    const std::string inputPath = valueOrEmpty(rows.front()[1]);
    if (inputPath.empty()) {
        error = "video has no source file";
        return false;
    }
    if (inputPath.rfind("uploads/", 0) != 0 ||
        inputPath.find("..") != std::string::npos) {
        error = "video source is outside the managed upload directory";
        return false;
    }
    std::string input;
    std::string output;
    const std::string outputPath = "uploads/transcoded/" + videoId + ".mp4";
    if (!database_.escape(inputPath, input, error) ||
        !database_.escape(outputPath, output, error)) {
        return false;
    }
    const std::string jobId = "transcode-" + videoId;
    std::string escapedJob;
    if (!database_.escape(jobId, escapedJob, error)) return false;
    const std::string sql =
        "INSERT INTO transcode_jobs (job_id, video_id, owner_account, "
        "input_path, output_path, status, attempts, max_attempts, "
        "next_attempt_at) VALUES ('" + escapedJob + "', '" + video +
        "', '" + owner + "', '" + input + "', '" + output +
        "', 'PENDING', 0, " + std::to_string(defaultMaxAttempts_) +
        ", NOW())";
    if (outbox_) {
#ifdef VOD_ENABLE_REFERENCE_RUNTIME
        vod::api::HlsTranscodeMessage message;
        message.set_video_id(videoId);
        message.set_source_file(inputPath);
        message.set_output_bundle_id("bundle-" + videoId);
        std::string messagePayload;
        if (!message.SerializeToString(&messagePayload)) {
            error = "failed to serialize transcode event payload";
            return false;
        }

        biteevent::OutboxEvent event;
        event.eventId = biteutil::Random::code(32);
        event.exchange = "vod.transcode";
        event.routingKey = "transcode.hls";
        event.eventType = "HLS_TRANSCODE_REQUESTED";
        event.aggregateType = "video";
        event.aggregateId = videoId;
        event.requestId = requestId;

        vod::api::EventEnvelope envelope;
        envelope.set_event_id(event.eventId);
        envelope.set_kind(vod::api::HLS_TRANSCODE_REQUESTED);
        envelope.set_aggregate_type(event.aggregateType);
        envelope.set_aggregate_id(event.aggregateId);
        envelope.set_request_id(requestId);
        envelope.set_payload(messagePayload);
        if (!envelope.SerializeToString(&event.payload)) {
            error = "failed to serialize transcode event envelope";
            return false;
        }
        std::string outboxSql;
        if (!outbox_->buildInsertSql(event, outboxSql, error) ||
            !database_.executeTransaction({sql, outboxSql}, error)) {
            return false;
        }
#else
        error = "outbox requires VOD_ENABLE_REFERENCE_RUNTIME";
        return false;
#endif
    } else if (!database_.execute(sql, error)) {
        return false;
    }
    std::optional<TranscodeJob> found;
    if (!readJob("video_id = '" + video + "'", found, error) || !found) {
        if (error.empty()) error = "transcode job was not created";
        return false;
    }
    job = *found;
    return true;
}

bool MySqlTranscodeRepository::findByVideoId(
    const std::string& videoId,
    const std::string& ownerAccount,
    std::optional<TranscodeJob>& job,
    std::string& error) {
    std::string video;
    std::string owner;
    if (!database_.escape(videoId, video, error) ||
        !database_.escape(ownerAccount, owner, error)) return false;
    return readJob("video_id = '" + video + "' AND owner_account = '" +
                       owner + "'",
                   job, error);
}

bool MySqlTranscodeRepository::retry(const std::string& videoId,
                                     const std::string& ownerAccount,
                                     bool& updated,
                                     std::string& error) {
    updated = false;
    bool changed = false;
    if (!database_.executeIfChangedPrepared(
            "UPDATE transcode_jobs SET status = 'PENDING', attempts = 0, "
            "lease_token = '', lease_until = NULL, next_attempt_at = NOW(), "
            "error_message = '', finished_at = NULL WHERE video_id = ? "
            "AND owner_account = ? AND status IN ('FAILED', 'SUCCEEDED')",
            {videoId, ownerAccount},
            "UPDATE videos SET transcode_status = 'PENDING' WHERE video_id = ?",
            {videoId}, changed, error)) return false;
    updated = changed;
    return true;
}

bool MySqlTranscodeRepository::recoverExpired(std::string& error) {
    return database_.executeTransaction({
        "UPDATE transcode_jobs SET "
        "status = IF(attempts < max_attempts, 'PENDING', 'FAILED'), "
        "next_attempt_at = NOW(), lease_token = '', lease_until = NULL, "
        "error_message = 'worker lease expired', "
        "finished_at = IF(attempts < max_attempts, NULL, NOW()) "
        "WHERE status = 'RUNNING' AND lease_until < NOW()",
        "UPDATE videos v INNER JOIN transcode_jobs j ON j.video_id = v.video_id "
        "SET v.transcode_status = 'FAILED' WHERE j.status = 'FAILED' AND "
        "j.error_message = 'worker lease expired'"}, error);
}

bool MySqlTranscodeRepository::claimNext(
    const std::string& leaseToken,
    int leaseSeconds,
    std::optional<TranscodeJob>& job,
    std::string& error) {
    job.reset();
    const int safeLease = std::max(1, leaseSeconds);
    unsigned long long affected = 0;
    const std::string sql =
        "UPDATE transcode_jobs SET status = 'RUNNING', lease_token = ?, "
        "lease_until = DATE_ADD(NOW(), INTERVAL " +
        std::to_string(safeLease) +
        " SECOND), attempts = attempts + 1, started_at = NOW(), "
        "error_message = '' WHERE status = 'PENDING' "
        "AND attempts < max_attempts AND next_attempt_at <= NOW() AND id = "
        "(SELECT id FROM (SELECT id FROM transcode_jobs WHERE status = "
        "'PENDING' AND attempts < max_attempts AND next_attempt_at <= NOW() "
        "ORDER BY next_attempt_at, id LIMIT 1) AS candidate)";
    if (!database_.executeAffectedPrepared(
            sql, {leaseToken}, affected, error)) return false;
    if (affected == 0) return true;
    std::string token;
    if (!database_.escape(leaseToken, token, error)) return false;
    return readJob("lease_token = '" + token + "'", job, error);
}

bool MySqlTranscodeRepository::renewLease(
    const std::string& jobId,
    const std::string& leaseToken,
    int leaseSeconds,
    bool& renewed,
    std::string& error) {
    renewed = false;
    unsigned long long affected = 0;
    if (!database_.executeAffectedPrepared(
            "UPDATE transcode_jobs SET lease_until = DATE_ADD(NOW(), "
            "INTERVAL " + std::to_string(std::max(1, leaseSeconds)) +
            " SECOND) WHERE job_id = ? AND lease_token = ? "
            "AND status = 'RUNNING'",
            {jobId, leaseToken}, affected, error)) return false;
    renewed = affected == 1;
    return true;
}

bool MySqlTranscodeRepository::markSucceeded(
    const TranscodeJob& job,
    const std::string& leaseToken,
    bool& updated,
    std::string& error) {
    updated = false;
    bool changed = false;
    if (!database_.executeIfChangedPrepared(
            "UPDATE transcode_jobs SET status = 'SUCCEEDED', "
            "lease_token = '', lease_until = NULL, finished_at = NOW(), "
            "error_message = '' "
            "WHERE job_id = ? AND lease_token = ? AND status = 'RUNNING'",
            {job.jobId, leaseToken},
            "UPDATE videos SET play_url = ?, transcode_status = 'READY' "
            "WHERE video_id = ?", {job.outputPath, job.videoId},
            changed, error)) return false;
    updated = changed;
    return true;
}

bool MySqlTranscodeRepository::markFailed(
    const TranscodeJob& job,
    const std::string& leaseToken,
    const std::string& reason,
    int retryDelaySeconds,
    bool& willRetry,
    std::string& error) {
    willRetry = false;
    const bool retry = job.attempts < job.maxAttempts;
    const std::string status = retry ? "PENDING" : "FAILED";
    const int delay = std::max(0, retryDelaySeconds);
    bool changed = false;
    const std::string followup =
        "UPDATE videos SET transcode_status = ? WHERE video_id = ?";
    if (!database_.executeIfChangedPrepared(
            "UPDATE transcode_jobs SET status = ?, lease_token = '', "
                "lease_until = NULL, error_message = ?, "
                "next_attempt_at = DATE_ADD(NOW(), INTERVAL " +
                std::to_string(delay) +
                " SECOND), finished_at = IF(? = 'FAILED', NOW(), NULL) "
                "WHERE job_id = ? AND lease_token = ? AND status = 'RUNNING'",
            {status, reason.substr(0, 512), status, job.jobId, leaseToken},
            followup, {status, job.videoId}, changed, error)) return false;
    willRetry = changed && retry;
    return true;
}

}  // namespace svc_transcode
