#pragma once

#include "../../database/database.h"
#include "../../svc_video/source/video_repository.h"

#include <memory>

namespace svc_video {

class VideoDataFacade {
public:
    explicit VideoDataFacade(bitedb::Database& database);

    std::unique_ptr<bitevideo::VideoStore> createRepository() const;

private:
    bitedb::Database& database_;
};

}  // namespace svc_video
