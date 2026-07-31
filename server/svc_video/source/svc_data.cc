#include "svc_data.h"

#include "video_repository.h"

namespace svc_video {

VideoDataFacade::VideoDataFacade(
    bitedb::Database& database,
    bitesearch::IVideoSearchIndex* searchIndex)
    : database_(database), searchIndex_(searchIndex) {}

std::unique_ptr<bitevideo::MySqlVideoRepository>
VideoDataFacade::createRepository() const {
    return std::make_unique<bitevideo::MySqlVideoRepository>(database_, searchIndex_);
}

}  // namespace svc_video
