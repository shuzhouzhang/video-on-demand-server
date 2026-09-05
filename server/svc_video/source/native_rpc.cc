#include "native_rpc.h"
#include <brpc/closure_guard.h>
namespace svc_video {
namespace {
void fillVideo(const bitevideo::Video& video, vod::api::VideoInfo& output) {
    output.set_video_id(video.id);
    output.set_title(video.title);
    output.set_uploader(video.userName);
    output.set_published_on(video.date);
    output.set_duration_seconds(video.durationSeconds);
    output.set_play_count(video.playCount);
    output.set_like_count(video.likeCount);
    output.set_category(video.category);
    output.set_description(video.description);
    for (const auto& tag : video.tags) output.add_tags(tag);
}
}
void NativeVideoService::ListVideos(google::protobuf::RpcController*,
    const vod::api::VideoListRequest* request, vod::api::VideoListResponse* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    auto* status = response->mutable_status();
    status->set_code(200);
    status->set_request_id(request->context().request_id());
    std::vector<bitevideo::Video> videos;
    std::string error;
    const bool ok = request->keyword().empty() ? repository_.list(videos,error) :
        repository_.search(request->keyword(),videos,error);
    if (!ok) {
        status->set_code(error.rfind("elasticsearch unavailable:",0)==0 ? 503 : 500);
        response->set_message(request->keyword().empty() ? "视频列表暂时不可用" : "视频搜索暂时不可用");
        return;
    }
    response->set_success(true);
    for (const auto& video : videos) fillVideo(video, *response->add_videos());
}
void NativeVideoService::GetVideoDetail(google::protobuf::RpcController*,
    const vod::api::VideoDetailRequest* request, vod::api::VideoDetailResponse* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    auto* status = response->mutable_status();
    status->set_code(200);
    status->set_request_id(request->context().request_id());
    if (request->video_id().empty()) { response->set_message("视频 id 不能为空"); return; }
    std::optional<bitevideo::Video> video;
    std::string error;
    if (!repository_.findById(request->video_id(),video,error)) {
        status->set_code(500); response->set_message("视频详情暂时不可用");
    } else if (!video) {
        response->set_message("视频不存在");
    } else {
        response->set_success(true); fillVideo(*video,*response->mutable_video());
    }
}
}
