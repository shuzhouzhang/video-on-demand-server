/*
 * 视频与互动数据访问实现。用户和后台能力位于各自 Repository 中。
 */
#pragma once

#include "../../database/database.h"
#include "../../repository/repository.h"

namespace bitesearch {
class IVideoSearchIndex;
}

namespace bitevideo {

class MySqlVideoRepository final : public biterepo::IVideoRepository,
                                   public biterepo::IInteractionRepository {
public:
    explicit MySqlVideoRepository(
        bitedb::Database& database,
        bitesearch::IVideoSearchIndex* searchIndex = nullptr);

    bool list(std::vector<Video>& videos, std::string& error) override;
    bool createVideo(const VideoDraft& draft,
                     std::optional<Video>& video,
                     std::string& error) override;
    bool findById(const std::string& videoId,
                  std::optional<Video>& video,
                  std::string& error) override;
    bool findAnyById(const std::string& videoId,
                     std::optional<Video>& video,
                     std::string& error) override;
    bool search(const std::string& keyword,
                std::vector<Video>& videos,
                std::string& error) override;
    bool playUrl(const std::string& videoId,
                 std::optional<std::string>& url,
                 std::string& error) override;
    bool ownerVideos(const std::string& account,
                     std::vector<Video>& videos,
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
    bool favoriteVideos(const std::string& account,
                        std::vector<Video>& videos,
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
    bool interactionUserProfile(
        const std::string& account,
        std::optional<UserProfile>& profile,
        std::string& error) override;

private:
    bitedb::Database& database_;
    bitesearch::IVideoSearchIndex* searchIndex_;
};

}  // namespace bitevideo
