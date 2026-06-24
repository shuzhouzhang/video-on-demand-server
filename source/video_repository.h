/*
 * 视频数据访问边界：HTTP层只依赖接口，MySQL实现负责把查询结果转成Video。
 */
#pragma once

#include "database.h"
#include "video.h"

#include <optional>
#include <string>
#include <vector>

namespace bitevideo {

class VideoStore {
public:
    virtual ~VideoStore() = default;
    virtual bool list(std::vector<Video>& videos, std::string& error) = 0;
    virtual bool findById(const std::string& videoId,
                          std::optional<Video>& video,
                          std::string& error) = 0;
};

class MySqlVideoRepository : public VideoStore {
public:
    explicit MySqlVideoRepository(bitedb::Database& database);
    bool list(std::vector<Video>& videos, std::string& error) override;
    bool findById(const std::string& videoId,
                  std::optional<Video>& video,
                  std::string& error) override;

private:
    bitedb::Database& database_;
};

}  // namespace bitevideo
