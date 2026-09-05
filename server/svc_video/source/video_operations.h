#pragma once
#include "video_routes.h"
#include "../../common/business_adapter.h"
namespace svc_video {
class VideoOperations final : public vod::business::VideoOperations {
  public:
    explicit VideoOperations(biteserver::RouteContext context)
        : handlers_(biteserver::makeVideoHandlers(context)) {}
    void ListVideos(google::protobuf::RpcController *,
                    const vod::business::ListVideosRequest *request,
                    vod::business::ListVideosResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.ListVideos, true,
                                false);
    }
    void CreateVideo(google::protobuf::RpcController *,
                     const vod::business::CreateVideoRequest *request,
                     vod::business::CreateVideoResponse *response,
                     google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.CreateVideo,
                                false, false);
    }
    void UploadVideo(google::protobuf::RpcController *,
                     const vod::business::UploadVideoRequest *request,
                     vod::business::UploadVideoResponse *response,
                     google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.UploadVideo,
                                false, true);
    }
    void GetDetail(google::protobuf::RpcController *,
                   const vod::business::GetDetailRequest *request,
                   vod::business::GetDetailResponse *response,
                   google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.GetDetail, true,
                                false);
    }
    void Search(google::protobuf::RpcController *,
                const vod::business::SearchRequest *request,
                vod::business::SearchResponse *response,
                google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.Search, true,
                                false);
    }
    void GetPlayUrl(google::protobuf::RpcController *,
                    const vod::business::GetPlayUrlRequest *request,
                    vod::business::GetPlayUrlResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.GetPlayUrl, true,
                                false);
    }
    void ListOwnerVideos(google::protobuf::RpcController *,
                         const vod::business::ListOwnerVideosRequest *request,
                         vod::business::ListOwnerVideosResponse *response,
                         google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.ListOwnerVideos,
                                true, false);
    }
    void ListReviews(google::protobuf::RpcController *,
                     const vod::business::ListReviewsRequest *request,
                     vod::business::ListReviewsResponse *response,
                     google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.ListReviews,
                                true, false);
    }
    void ReviewVideo(google::protobuf::RpcController *,
                     const vod::business::ReviewVideoRequest *request,
                     vod::business::ReviewVideoResponse *response,
                     google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.ReviewVideo,
                                false, false);
    }

  private:
    biteserver::VideoHandlers handlers_;
};
} // namespace svc_video
