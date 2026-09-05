#pragma once
#include "../../server/repository/repository.h"
#include <mutex>
#include <unordered_map>
class FakeRepositories : public biterepo::IUserRepository,
                         public biterepo::IVideoRepository,
                         public biterepo::IInteractionRepository,
                         public biterepo::IAdminRepository {
public:
    bool list(std::vector<bitevideo::Video>& videos,
              std::string& error) override {
        error.clear();
        videos = {baseVideo_};
        return true;
    }

    bool createVideo(const bitevideo::VideoDraft& draft,
                     std::optional<bitevideo::Video>& video,
                     std::string& error) override {
        error.clear();
        const std::string videoId = nextCreatedVideoIndex_ < 10 ?
            "video-00" + std::to_string(nextCreatedVideoIndex_++) :
            "video-0" + std::to_string(nextCreatedVideoIndex_++);
        createdVideo_ = bitevideo::Video{
            videoId, draft.title, draft.userName, "6-25", 0,
            "0", "0", draft.category, draft.tags, draft.description};
        createdVideos_[videoId] = createdVideo_;
        createdOwners_[videoId] = draft.account;
        createdPlayUrls_[videoId] = draft.playUrl.empty() ? draft.videoFileName :
            draft.playUrl;
        video = createdVideo_;
        return true;
    }

    bool findById(const std::string& videoId,
                  std::optional<bitevideo::Video>& video,
                  std::string& error) override {
        error.clear();
        if (videoId == "video-001") {
            video = baseVideo_;
        } else {
            video.reset();
        }
        return true;
    }

    bool findAnyById(const std::string& videoId,
                     std::optional<bitevideo::Video>& video,
                     std::string& error) override {
        error.clear();
        if (videoId == "video-001") {
            video = baseVideo_;
        } else if (createdVideos_.count(videoId) > 0) {
            video = createdVideos_.at(videoId);
        } else {
            video.reset();
        }
        return true;
    }

    bool search(const std::string& keyword,
                std::vector<bitevideo::Video>& videos,
                std::string& error) override {
        error.clear();
        videos.clear();
        if (keyword == "测试" || keyword == "科技") {
            list(videos, error);
        }
        return true;
    }

    bool playUrl(const std::string& videoId,
                 std::optional<std::string>& url,
                 std::string& error) override {
        error.clear();
        if (videoId == "video-001") {
            url = "D:/video-on-demand-client/test.mp4";
        } else {
            url.reset();
        }
        return true;
    }

    bool likeStatus(const std::string& videoId,
                    const std::string&,
                    std::optional<bitevideo::LikeStatus>& status,
                    std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            status.reset();
        } else {
            status = bitevideo::LikeStatus{liked_, std::to_string(likeCount_)};
        }
        return true;
    }

    bool setLiked(const std::string& videoId,
                  const std::string& account,
                  bool shouldLike,
                  std::optional<bitevideo::LikeStatus>& status,
                  std::string& error) override {
        if (!likeStatus(videoId, account, status, error) || !status) {
            return true;
        }
        if (shouldLike && !liked_) {
            liked_ = true;
            ++likeCount_;
        } else if (!shouldLike && liked_) {
            liked_ = false;
            --likeCount_;
        }
        status = bitevideo::LikeStatus{liked_, std::to_string(likeCount_)};
        return true;
    }

    bool watchProgress(const std::string& videoId,
                       const std::string&,
                       std::optional<bitevideo::WatchProgress>& progress,
                       std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            progress.reset();
        } else {
            progress = bitevideo::WatchProgress{watchSeconds_};
        }
        return true;
    }

    bool saveWatchProgress(const std::string& videoId,
                           const std::string& account,
                           int seconds,
                           std::optional<bitevideo::WatchProgress>& progress,
                           std::string& error) override {
        if (!watchProgress(videoId, account, progress, error) || !progress) {
            return true;
        }
        watchSeconds_ = seconds;
        progress = bitevideo::WatchProgress{watchSeconds_};
        return true;
    }

    bool favoriteStatus(const std::string& videoId,
                        const std::string&,
                        std::optional<bitevideo::FavoriteStatus>& status,
                        std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            status.reset();
        } else {
            status = bitevideo::FavoriteStatus{favorited_};
        }
        return true;
    }

    bool setFavorited(const std::string& videoId,
                      const std::string& account,
                      bool shouldFavorite,
                      std::optional<bitevideo::FavoriteStatus>& status,
                      std::string& error) override {
        if (!favoriteStatus(videoId, account, status, error) || !status) {
            return true;
        }
        favorited_ = shouldFavorite;
        status = bitevideo::FavoriteStatus{favorited_};
        return true;
    }

    bool favoriteVideos(const std::string&,
                        std::vector<bitevideo::Video>& videos,
                        std::string& error) override {
        error.clear();
        videos.clear();
        if (favorited_) {
            list(videos, error);
        }
        return true;
    }

    bool ownerVideos(const std::string& account,
                     std::vector<bitevideo::Video>& videos,
                     std::string& error) override {
        error.clear();
        videos.clear();
        if (account == "bit-user-001") {
            list(videos, error);
            for (const auto& [videoId, owner] : createdOwners_) {
                if (owner == account) {
                    videos.push_back(createdVideos_.at(videoId));
                }
            }
        }
        return true;
    }

    bool comments(const std::string& videoId,
                  std::optional<std::vector<bitevideo::VideoComment>>& comments,
                  std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            comments.reset();
        } else {
            comments = comments_;
        }
        return true;
    }

    bool addComment(const std::string& videoId,
                    const std::string& userName,
                    const std::string& account,
                    const std::string& content,
                    std::optional<bitevideo::VideoComment>& comment,
                    std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            comment.reset();
            return true;
        }
        bitevideo::VideoComment saved{
            "comment-001", videoId, userName, account, content,
            "2026-06-25 11:40"};
        comments_.insert(comments_.begin(), saved);
        comment = saved;
        return true;
    }

    bool barrages(const std::string& videoId,
                  std::optional<std::vector<bitevideo::VideoBarrage>>& barrages,
                  std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            barrages.reset();
        } else {
            barrages = barrages_;
        }
        return true;
    }

    bool addBarrage(const std::string& videoId,
                    int seconds,
                    const std::string& text,
                    std::optional<bitevideo::VideoBarrage>& barrage,
                    std::string& error) override {
        error.clear();
        if (videoId != "video-001") {
            barrage.reset();
            return true;
        }
        bitevideo::VideoBarrage saved{seconds, text};
        barrages_.push_back(saved);
        barrage = saved;
        return true;
    }

    bool userProfile(const std::string& account,
                     std::optional<bitevideo::UserProfile>& profile,
                     std::string& error) override {
        error.clear();
        if (account != "bit-user-001") {
            profile.reset();
        } else {
            profile = user_;
        }
        return true;
    }

    bool interactionUserProfile(
        const std::string& account,
        std::optional<bitevideo::UserProfile>& profile,
        std::string& error) override {
        return userProfile(account, profile, error);
    }

    bool userAccess(const std::string& account,
                    std::optional<bitevideo::UserAccess>& access,
                    std::string& error) override {
        error.clear();
        access.reset();
        if (account == "disabled-admin@bit.com") {
            access = bitevideo::UserAccess{"管理员", "禁用"};
            return true;
        }
        for (const auto& user : users_) {
            if (user.account == account) {
                access = bitevideo::UserAccess{user.role, user.status};
                break;
            }
        }
        return true;
    }

    bool updateUserProfile(const std::string& account,
                           const std::string& userName,
                           const std::string& description,
                           std::optional<bitevideo::UserProfile>& profile,
                           std::string& error) override {
        error.clear();
        if (account != "bit-user-001") {
            profile.reset();
            return true;
        }
        user_.userName = userName;
        user_.description = description;
        profile = user_;
        return true;
    }

    bool updateAvatarPath(const std::string& account,
                          const std::string& avatarPath,
                          bool& updated,
                          std::string& error) override {
        error.clear();
        updated = false;
        if (account != user_.account) {
            return true;
        }
        user_.avatarPath = avatarPath;
        updated = true;
        return true;
    }

    bool passwordLogin(const std::string& account,
                       const std::string& password,
                       std::optional<bitevideo::UserProfile>& profile,
                       std::string& error) override {
        error.clear();
        if (account == user_.account && password == "123456") {
            profile = user_;
        } else {
            profile.reset();
        }
        return true;
    }

    bool createEmailCode(const std::string& email,
                         bitevideo::EmailCodeSession& session,
                         std::string& error) override {
        error.clear();
        if (email.find('@') == std::string::npos) {
            session = {};
            error = "邮箱格式错误";
        } else {
            std::lock_guard<std::mutex> lock(emailMutex_);
            email_ = email;
            emailCodeConsumed_ = false;
            emailFailedAttempts_ = 0;
            session = bitevideo::EmailCodeSession{"email-code-001", "246810"};
        }
        return true;
    }

    bool emailLogin(const std::string& email,
                    const std::string& authcodeId,
                    const std::string& authcode,
                    std::optional<bitevideo::UserProfile>& profile,
                    std::string& error) override {
        error.clear();
        std::lock_guard<std::mutex> lock(emailMutex_);
        if (email != email_ || authcodeId != "email-code-001" ||
            emailCodeConsumed_ || emailFailedAttempts_ >= 5) {
            profile.reset();
            return true;
        }
        if (authcode != "246810") {
            ++emailFailedAttempts_;
            profile.reset();
            return true;
        }
        emailCodeConsumed_ = true;
        profile = bitevideo::UserProfile{email, "email-user", "", ""};
        return true;
    }

    bool logout(const std::string& account,
                bool& knownUser,
                std::string& error) override {
        error.clear();
        knownUser = account == user_.account || account == email_;
        return true;
    }

    bool adminReviews(std::vector<bitevideo::AdminReview>& reviews,
                      std::string& error) override {
        error.clear();
        reviews = {review_};
        return true;
    }

    bool updateReviewStatus(const std::string& videoId,
                            const std::string& status,
                            bool& updated,
                            std::string& error) override {
        error.clear();
        updated = false;
        if (videoId != review_.videoId ||
            (status != "审核通过" && status != "审核拒绝")) {
            error = "审核参数错误";
            return true;
        }
        review_.status = status;
        updated = true;
        return true;
    }

    bool adminUsers(std::vector<bitevideo::AdminUser>& users,
                    std::string& error) override {
        error.clear();
        users = users_;
        return true;
    }

    bool updateAdminUser(const std::string& account,
                         const std::string& action,
                         bool& updated,
                         std::string& error) override {
        error.clear();
        updated = false;
        for (auto it = users_.begin(); it != users_.end(); ++it) {
            if (it->account != account) {
                continue;
            }
            if (action == "set-admin") {
                it->role = "管理员";
            } else if (action == "disable") {
                it->status = "禁用";
            } else if (action == "enable") {
                it->status = "启用";
            } else if (action == "delete") {
                users_.erase(it);
            } else {
                error = "角色操作不支持";
                return true;
            }
            updated = true;
            return true;
        }
        error = "用户不存在";
        return true;
    }

    bool smokeCleanup(const std::string&,
                      const std::string&,
                      const std::string&,
                      const std::string&,
                      std::string& error) override {
        error.clear();
        return true;
    }

private:
    bitevideo::Video baseVideo_{
        "video-001", "测试视频", "测试用户", "6-23", 558,
        "36000", "256", "科技", {"编程开发", "软件工具"},
        "HTTP测试数据"};
    bitevideo::Video createdVideo_;
    int nextCreatedVideoIndex_ = 3;
    std::unordered_map<std::string, bitevideo::Video> createdVideos_;
    std::unordered_map<std::string, std::string> createdPlayUrls_;
    std::unordered_map<std::string, std::string> createdOwners_;
    bool liked_ = false;
    int likeCount_ = 256;
    int watchSeconds_ = 0;
    bool favorited_ = false;
    std::vector<bitevideo::VideoComment> comments_;
    std::vector<bitevideo::VideoBarrage> barrages_;
    bitevideo::UserProfile user_{
        "bit-user-001", "BIT 用户", "真实后端用户资料", ""};
    std::string email_;
    std::mutex emailMutex_;
    bool emailCodeConsumed_ = false;
    int emailFailedAttempts_ = 0;
    bitevideo::AdminReview review_{
        "video-001", "测试视频", "bit-user-001", "待审核",
        "2026-06-25 12:00"};
    std::vector<bitevideo::AdminUser> users_{
        {"admin@bit.com", "系统管理员", "超级管理员", "启用",
         "2026-05-01 10:00"},
        {"bit-user-001", "BIT 用户", "普通用户", "启用",
         "2026-06-01 09:00"}};
};
