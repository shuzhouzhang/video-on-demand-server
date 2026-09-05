#pragma once
#include "svc_data.h"
#include "business.pb.h"
#include <brpc/closure_guard.h>
namespace svc_transcode {
class TranscodeOperations final : public vod::business::TranscodeOperations {
  public:
    TranscodeOperations(ITranscodeRepository &repository, bool strict)
        : repository_(repository), strict_(strict) {}
    template <class Request, class Response>
    void execute(const Request &request, Response &response, int operation) {
        auto *status = response.mutable_status();
        status->set_code(200);
        status->set_request_id(request.context().request_id());
        auto *result = response.mutable_result();
        result->set_success(false);
        auto fail = [&](int code, const std::string &message) {
            status->set_code(code);
            result->set_message(message);
        };
        const auto &payload = request.payload();
        auto account = payload.account();
        if (strict_) {
            const auto &authenticated =
                request.context().authenticated_account();
            if (authenticated.empty()) {
                fail(401, "authentication required");
                return;
            }
            if (!account.empty() && account != authenticated) {
                fail(403, "account does not match authenticated user");
                return;
            }
            account = authenticated;
        }
        if (account.empty() || payload.videoid().empty()) {
            fail(400, "videoId and account are required");
            return;
        }
        std::string error;
        std::optional<TranscodeJob> job;
        if (operation == 0) {
            TranscodeJob value;
            if (!repository_.enqueueForVideo(payload.videoid(), account,
                                             request.context().request_id(),
                                             value, error)) {
                fail(error.find("not found") != std::string::npos ? 404 : 500,
                     error);
                return;
            }
            job = value;
            status->set_code(202);
        } else if (operation == 1) {
            if (!repository_.findByVideoId(payload.videoid(), account, job,
                                           error)) {
                fail(500, error);
                return;
            }
            if (!job) {
                fail(404, "transcode job not found");
                return;
            }
        } else {
            bool updated = false;
            if (!repository_.retry(payload.videoid(), account, updated,
                                   error)) {
                fail(500, error);
                return;
            }
            if (!updated) {
                fail(409, "only completed or failed jobs can be retried");
                return;
            }
            result->set_success(true);
            result->set_message("transcode job queued for retry");
            result->mutable_data()->set_videoid(payload.videoid());
            result->mutable_data()->set_status("PENDING");
            status->set_code(202);
            return;
        }
        result->set_success(true);
        result->set_message(operation == 0 ? "transcode job accepted" : "ok");
        auto *data = result->mutable_data();
        data->set_jobid(job->jobId);
        data->set_videoid(job->videoId);
        data->set_status(job->status);
        data->set_attempts(job->attempts);
        data->set_maxattempts(job->maxAttempts);
        data->set_error(job->errorMessage);
    }
    void SubmitJob(google::protobuf::RpcController *,
                   const vod::business::SubmitJobRequest *q,
                   vod::business::SubmitJobResponse *p,
                   google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        execute(*q, *p, 0);
    }
    void GetJob(google::protobuf::RpcController *,
                const vod::business::GetJobRequest *q,
                vod::business::GetJobResponse *p,
                google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        execute(*q, *p, 1);
    }
    void RetryJob(google::protobuf::RpcController *,
                  const vod::business::RetryJobRequest *q,
                  vod::business::RetryJobResponse *p,
                  google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        execute(*q, *p, 2);
    }

  private:
    ITranscodeRepository &repository_;
    bool strict_;
};
} // namespace svc_transcode
