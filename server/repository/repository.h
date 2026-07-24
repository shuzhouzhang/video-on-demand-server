#pragma once

#include "../data/admin_review.h"
#include "../data/user.h"
#include "../data/video_draft.h"
#include "../data/video_interaction.h"
#include "../svc_video/source/video.h"

#include <optional>
#include <string>
#include <vector>

namespace biterepo {

class IUserRepository {
public:
    virtual ~IUserRepository() = default;
    virtual bool userProfile(const std::string& account,
                             std::optional<bitevideo::UserProfile>& profile,
                             std::string& error) = 0;
    virtual bool updateUserProfile(
        const std::string& account,
        const std::string& userName,
        const std::string& description,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) = 0;
    virtual bool updateAvatarPath(const std::string& account,
                                  const std::string& avatarPath,
                                  bool& updated,
                                  std::string& error) = 0;
    virtual bool passwordLogin(
        const std::string& account,
        const std::string& password,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) = 0;
    virtual bool createEmailCode(const std::string& email,
                                 bitevideo::EmailCodeSession& session,
                                 std::string& error) = 0;
    virtual bool emailLogin(
        const std::string& email,
        const std::string& authcodeId,
        const std::string& authcode,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) = 0;
    virtual bool logout(const std::string& account,
                        bool& knownUser,
                        std::string& error) = 0;
};

class IVideoRepository {
public:
    virtual ~IVideoRepository() = default;
    virtual bool list(std::vector<bitevideo::Video>& videos,
                      std::string& error) = 0;
    virtual bool createVideo(
        const bitevideo::VideoDraft& draft,
        std::optional<bitevideo::Video>& video,
        std::string& error) = 0;
    virtual bool findById(const std::string& videoId,
                          std::optional<bitevideo::Video>& video,
                          std::string& error) = 0;
    virtual bool findAnyById(const std::string& videoId,
                             std::optional<bitevideo::Video>& video,
                             std::string& error) = 0;
    virtual bool search(const std::string& keyword,
                        std::vector<bitevideo::Video>& videos,
                        std::string& error) = 0;
    virtual bool playUrl(const std::string& videoId,
                         std::optional<std::string>& url,
                         std::string& error) = 0;
    virtual bool ownerVideos(const std::string& account,
                             std::vector<bitevideo::Video>& videos,
                             std::string& error) = 0;
};

class IInteractionRepository {
public:
    virtual ~IInteractionRepository() = default;
    virtual bool likeStatus(
        const std::string& videoId,
        const std::string& account,
        std::optional<bitevideo::LikeStatus>& status,
        std::string& error) = 0;
    virtual bool setLiked(
        const std::string& videoId,
        const std::string& account,
        bool shouldLike,
        std::optional<bitevideo::LikeStatus>& status,
        std::string& error) = 0;
    virtual bool watchProgress(
        const std::string& videoId,
        const std::string& account,
        std::optional<bitevideo::WatchProgress>& progress,
        std::string& error) = 0;
    virtual bool saveWatchProgress(
        const std::string& videoId,
        const std::string& account,
        int seconds,
        std::optional<bitevideo::WatchProgress>& progress,
        std::string& error) = 0;
    virtual bool favoriteStatus(
        const std::string& videoId,
        const std::string& account,
        std::optional<bitevideo::FavoriteStatus>& status,
        std::string& error) = 0;
    virtual bool setFavorited(
        const std::string& videoId,
        const std::string& account,
        bool shouldFavorite,
        std::optional<bitevideo::FavoriteStatus>& status,
        std::string& error) = 0;
    virtual bool favoriteVideos(const std::string& account,
                                std::vector<bitevideo::Video>& videos,
                                std::string& error) = 0;
    virtual bool comments(
        const std::string& videoId,
        std::optional<std::vector<bitevideo::VideoComment>>& comments,
        std::string& error) = 0;
    virtual bool addComment(
        const std::string& videoId,
        const std::string& userName,
        const std::string& account,
        const std::string& content,
        std::optional<bitevideo::VideoComment>& comment,
        std::string& error) = 0;
    virtual bool barrages(
        const std::string& videoId,
        std::optional<std::vector<bitevideo::VideoBarrage>>& barrages,
        std::string& error) = 0;
    virtual bool addBarrage(
        const std::string& videoId,
        int seconds,
        const std::string& text,
        std::optional<bitevideo::VideoBarrage>& barrage,
        std::string& error) = 0;
    // Video interactions need the authoritative display name for comments,
    // but do not expose profile mutation operations.
    virtual bool interactionUserProfile(
        const std::string& account,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) = 0;
};

class IAdminRepository {
public:
    virtual ~IAdminRepository() = default;
    virtual bool userAccess(
        const std::string& account,
        std::optional<bitevideo::UserAccess>& access,
        std::string& error) = 0;
    virtual bool adminReviews(std::vector<bitevideo::AdminReview>& reviews,
                              std::string& error) = 0;
    virtual bool updateReviewStatus(const std::string& videoId,
                                    const std::string& status,
                                    bool& updated,
                                    std::string& error) = 0;
    virtual bool adminUsers(std::vector<bitevideo::AdminUser>& users,
                            std::string& error) = 0;
    virtual bool updateAdminUser(const std::string& account,
                                 const std::string& action,
                                 bool& updated,
                                 std::string& error) = 0;
    virtual bool smokeCleanup(const std::string& videoId,
                              const std::string& videoTitle,
                              const std::string& account,
                              const std::string& previousAvatarPath,
                              std::string& error) = 0;
};

// Services receive only the domain capabilities needed by their registered
// routes. The compatibility video_server composes all four implementations.
struct RepositorySet {
    IUserRepository* users = nullptr;
    IVideoRepository* videos = nullptr;
    IInteractionRepository* interactions = nullptr;
    IAdminRepository* admins = nullptr;
};

}  // namespace biterepo
