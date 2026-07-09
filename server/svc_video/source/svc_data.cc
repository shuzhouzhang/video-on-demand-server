#include "svc_data.h"

#include "video_repository.h"

namespace svc_video {

VideoDataFacade::VideoDataFacade(bitedb::Database& database) : database_(database) {}

std::unique_ptr<bitevideo::VideoStore> VideoDataFacade::createRepository() const {
    return std::make_unique<bitevideo::MySqlVideoRepository>(database_);
}

}  // namespace svc_video
