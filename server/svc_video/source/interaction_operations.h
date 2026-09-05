#pragma once
#include "interaction_routes.h"
#include "../../common/business_adapter.h"
namespace svc_video {
class InteractionOperations final
    : public vod::business::InteractionOperations {
  public:
    explicit InteractionOperations(biteserver::RouteContext context)
        : handlers_(biteserver::makeInteractionHandlers(context)) {}
    void GetLike(google::protobuf::RpcController *,
                 const vod::business::GetLikeRequest *request,
                 vod::business::GetLikeResponse *response,
                 google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.GetLike, true,
                                false);
    }
    void Like(google::protobuf::RpcController *,
              const vod::business::LikeRequest *request,
              vod::business::LikeResponse *response,
              google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.Like, false,
                                false);
    }
    void Unlike(google::protobuf::RpcController *,
                const vod::business::UnlikeRequest *request,
                vod::business::UnlikeResponse *response,
                google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.Unlike, false,
                                false);
    }
    void GetProgress(google::protobuf::RpcController *,
                     const vod::business::GetProgressRequest *request,
                     vod::business::GetProgressResponse *response,
                     google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.GetProgress,
                                true, false);
    }
    void SaveProgress(google::protobuf::RpcController *,
                      const vod::business::SaveProgressRequest *request,
                      vod::business::SaveProgressResponse *response,
                      google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.SaveProgress,
                                false, false);
    }
    void GetFavorite(google::protobuf::RpcController *,
                     const vod::business::GetFavoriteRequest *request,
                     vod::business::GetFavoriteResponse *response,
                     google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.GetFavorite,
                                true, false);
    }
    void Favorite(google::protobuf::RpcController *,
                  const vod::business::FavoriteRequest *request,
                  vod::business::FavoriteResponse *response,
                  google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.Favorite, false,
                                false);
    }
    void Unfavorite(google::protobuf::RpcController *,
                    const vod::business::UnfavoriteRequest *request,
                    vod::business::UnfavoriteResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.Unfavorite,
                                false, false);
    }
    void ListFavorites(google::protobuf::RpcController *,
                       const vod::business::ListFavoritesRequest *request,
                       vod::business::ListFavoritesResponse *response,
                       google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.ListFavorites,
                                true, false);
    }
    void ListComments(google::protobuf::RpcController *,
                      const vod::business::ListCommentsRequest *request,
                      vod::business::ListCommentsResponse *response,
                      google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.ListComments,
                                true, false);
    }
    void AddComment(google::protobuf::RpcController *,
                    const vod::business::AddCommentRequest *request,
                    vod::business::AddCommentResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.AddComment,
                                false, false);
    }
    void ListBarrages(google::protobuf::RpcController *,
                      const vod::business::ListBarragesRequest *request,
                      vod::business::ListBarragesResponse *response,
                      google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.ListBarrages,
                                true, false);
    }
    void AddBarrage(google::protobuf::RpcController *,
                    const vod::business::AddBarrageRequest *request,
                    vod::business::AddBarrageResponse *response,
                    google::protobuf::Closure *done) override {
        brpc::ClosureGuard guard(done);
        biterpc::invokeBusiness(*request, *response, handlers_.AddBarrage,
                                false, false);
    }

  private:
    biteserver::InteractionHandlers handlers_;
};
} // namespace svc_video
