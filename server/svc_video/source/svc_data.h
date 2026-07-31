#pragma once

#include "../../database/database.h"
#include "../../svc_video/source/video_repository.h"

#include <memory>

namespace svc_video {

class VideoDataFacade {
public:
    explicit VideoDataFacade(
        bitedb::Database& database,
        bitesearch::IVideoSearchIndex* searchIndex = nullptr);

    std::unique_ptr<bitevideo::MySqlVideoRepository> createRepository() const;

private:
    bitedb::Database& database_;
    bitesearch::IVideoSearchIndex* searchIndex_;
};

}  // namespace svc_video
