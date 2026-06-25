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

struct LikeStatus {
    bool liked;
    std::string likeCount;
};

struct WatchProgress {
    int seconds;
};

struct FavoriteStatus {
    bool favorited;
};

struct VideoComment {
    std::string id;
    std::string videoId;
    std::string userName;
    std::string account;
    std::string content;
    std::string createdAt;
};

struct VideoBarrage {
    int seconds;
    std::string text;
};

class VideoStore {
public:
    virtual ~VideoStore() = default;
    virtual bool list(std::vector<Video>& videos, std::string& error) = 0;
    virtual bool findById(const std::string& videoId,
                          std::optional<Video>& video,
                          std::string& error) = 0;
    virtual bool search(const std::string& keyword,
                        std::vector<Video>& videos,
                        std::string& error) = 0;
    virtual bool playUrl(const std::string& videoId,
                         std::optional<std::string>& url,
                         std::string& error) = 0;
    virtual bool likeStatus(const std::string& videoId,
                            const std::string& account,
                            std::optional<LikeStatus>& status,
                            std::string& error) = 0;
    virtual bool setLiked(const std::string& videoId,
                          const std::string& account,
                          bool shouldLike,
                          std::optional<LikeStatus>& status,
                          std::string& error) = 0;
    virtual bool watchProgress(const std::string& videoId,
                               const std::string& account,
                               std::optional<WatchProgress>& progress,
                               std::string& error) = 0;
    virtual bool saveWatchProgress(const std::string& videoId,
                                   const std::string& account,
                                   int seconds,
                                   std::optional<WatchProgress>& progress,
                                   std::string& error) = 0;
    virtual bool favoriteStatus(const std::string& videoId,
                                const std::string& account,
                                std::optional<FavoriteStatus>& status,
                                std::string& error) = 0;
    virtual bool setFavorited(const std::string& videoId,
                              const std::string& account,
                              bool shouldFavorite,
                              std::optional<FavoriteStatus>& status,
                              std::string& error) = 0;
    virtual bool comments(const std::string& videoId,
                          std::optional<std::vector<VideoComment>>& comments,
                          std::string& error) = 0;
    virtual bool addComment(const std::string& videoId,
                            const std::string& userName,
                            const std::string& account,
                            const std::string& content,
                            std::optional<VideoComment>& comment,
                            std::string& error) = 0;
    virtual bool barrages(const std::string& videoId,
                          std::optional<std::vector<VideoBarrage>>& barrages,
                          std::string& error) = 0;
    virtual bool addBarrage(const std::string& videoId,
                            int seconds,
                            const std::string& text,
                            std::optional<VideoBarrage>& barrage,
                            std::string& error) = 0;
};

class MySqlVideoRepository : public VideoStore {
public:
    explicit MySqlVideoRepository(bitedb::Database& database);
    bool list(std::vector<Video>& videos, std::string& error) override;
    bool findById(const std::string& videoId,
                  std::optional<Video>& video,
                  std::string& error) override;
    bool search(const std::string& keyword,
                std::vector<Video>& videos,
                std::string& error) override;
    bool playUrl(const std::string& videoId,
                 std::optional<std::string>& url,
                 std::string& error) override;
    bool likeStatus(const std::string& videoId,
                    const std::string& account,
                    std::optional<LikeStatus>& status,
                    std::string& error) override;
    bool setLiked(const std::string& videoId,
                  const std::string& account,
                  bool shouldLike,
                  std::optional<LikeStatus>& status,
                  std::string& error) override;
    bool watchProgress(const std::string& videoId,
                       const std::string& account,
                       std::optional<WatchProgress>& progress,
                       std::string& error) override;
    bool saveWatchProgress(const std::string& videoId,
                           const std::string& account,
                           int seconds,
                           std::optional<WatchProgress>& progress,
                           std::string& error) override;
    bool favoriteStatus(const std::string& videoId,
                        const std::string& account,
                        std::optional<FavoriteStatus>& status,
                        std::string& error) override;
    bool setFavorited(const std::string& videoId,
                      const std::string& account,
                      bool shouldFavorite,
                      std::optional<FavoriteStatus>& status,
                      std::string& error) override;
    bool comments(const std::string& videoId,
                  std::optional<std::vector<VideoComment>>& comments,
                  std::string& error) override;
    bool addComment(const std::string& videoId,
                    const std::string& userName,
                    const std::string& account,
                    const std::string& content,
                    std::optional<VideoComment>& comment,
                    std::string& error) override;
    bool barrages(const std::string& videoId,
                  std::optional<std::vector<VideoBarrage>>& barrages,
                  std::string& error) override;
    bool addBarrage(const std::string& videoId,
                    int seconds,
                    const std::string& text,
                    std::optional<VideoBarrage>& barrage,
                    std::string& error) override;

private:
    bitedb::Database& database_;
};

}  // namespace bitevideo
