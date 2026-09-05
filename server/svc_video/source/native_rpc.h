#pragma once
#include "video.pb.h"
#include "../../repository/repository.h"
namespace svc_video {
// 视频查询直接使用领域仓库，协议层不执行 SQL，也不连接本地 HTTP 端口。
class NativeVideoService final : public vod::api::VideoService {
public:
    explicit NativeVideoService(biterepo::IVideoRepository& repository) : repository_(repository) {}
    void ListVideos(google::protobuf::RpcController*, const vod::api::VideoListRequest*,
                    vod::api::VideoListResponse*, google::protobuf::Closure*) override;
    void GetVideoDetail(google::protobuf::RpcController*, const vod::api::VideoDetailRequest*,
                        vod::api::VideoDetailResponse*, google::protobuf::Closure*) override;
private:
    biterepo::IVideoRepository& repository_;
};
}
