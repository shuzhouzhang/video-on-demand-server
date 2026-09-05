#include "interaction_routes.h"
#include "../../common/auth.h"
#include "../../common/bitelog.h"
#include "../../common/email_verification.h"
#include "../../common/session_token.h"
#include "../../common/util.h"
#include <algorithm>
#include <filesystem>
#include <cstdlib>
namespace biteserver {
using namespace detail;
void registerInteractionRoutes(httplib::Server& server, RouteContext context) {
    if (!context.repositories.interactions) return;
    server.Get("/videos/like-status", [context](const httplib::Request& request,
                                              httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string claimedAccount = request.has_param("account")
            ? request.get_param_value("account") : "";
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (account.empty()) {
            account = "guest";
        }
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::LikeStatus> status;
        std::string error;
        if (!context.repositories.interactions->likeStatus(videoId, account, status, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/like-status failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "点赞状态暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!status) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["liked"] = status->liked;
            body["likeCount"] = status->likeCount;
            setJsonResponse(response, 200, body);
        }
    });

    const auto changeLike = [context](const httplib::Request& request,
                                   httplib::Response& response,
                                   bool shouldLike) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        const std::string claimedAccount = (*payload)["account"].asString();
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (account.empty()) {
            account = "guest";
        }
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::LikeStatus> status;
        std::string error;
        if (!context.repositories.interactions->setLiked(videoId, account, shouldLike, status, error)) {
            if (bitelog::g_logger) {
                ERR("video like change failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "点赞操作暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!status) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["liked"] = status->liked;
            body["likeCount"] = status->likeCount;
            setJsonResponse(response, 200, body);
        }
    };

    server.Post("/videos/like",
                 [changeLike](const httplib::Request& request,
                              httplib::Response& response) {
                     changeLike(request, response, true);
                 });
    server.Post("/videos/unlike",
                 [changeLike](const httplib::Request& request,
                              httplib::Response& response) {
                     changeLike(request, response, false);
                 });

    server.Get("/videos/watch-progress",
                [context](const httplib::Request& request,
                       httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string claimedAccount = request.has_param("account")
            ? request.get_param_value("account") : "";
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (account.empty()) {
            account = "guest";
        }
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::WatchProgress> progress;
        std::string error;
        if (!context.repositories.interactions->watchProgress(videoId, account, progress, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/watch-progress failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "播放进度暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!progress) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["seconds"] = progress->seconds;
            body["message"] = "读取成功";
            setJsonResponse(response, 200, body);
        }
    });

    server.Post("/videos/watch-progress",
                 [context](const httplib::Request& request,
                        httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        const std::string claimedAccount = (*payload)["account"].asString();
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (account.empty()) {
            account = "guest";
        }
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!(*payload)["seconds"].isInt() || (*payload)["seconds"].asInt() < 0) {
            body["success"] = false;
            body["message"] = "播放秒数非法";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::WatchProgress> progress;
        std::string error;
        if (!context.repositories.interactions->saveWatchProgress(
                videoId, account, (*payload)["seconds"].asInt(), progress, error)) {
            if (bitelog::g_logger) {
                ERR("POST /videos/watch-progress failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "播放进度保存失败";
            setJsonResponse(response, 500, body);
        } else if (!progress) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["seconds"] = progress->seconds;
            body["message"] = "保存成功";
            setJsonResponse(response, 200, body);
        }
    });

    server.Get("/videos/favorite-status",
                [context](const httplib::Request& request,
                       httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string claimedAccount = request.has_param("account")
            ? request.get_param_value("account") : "";
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (videoId.empty() || account.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 和账号不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::FavoriteStatus> status;
        std::string error;
        if (!context.repositories.interactions->favoriteStatus(videoId, account, status, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/favorite-status failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "收藏状态暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!status) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["favorited"] = status->favorited;
            setJsonResponse(response, 200, body);
        }
    });

    const auto changeFavorite = [context](const httplib::Request& request,
                                       httplib::Response& response,
                                       bool shouldFavorite) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        const std::string claimedAccount = (*payload)["account"].asString();
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后再收藏视频";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::FavoriteStatus> status;
        std::string error;
        if (!context.repositories.interactions->setFavorited(
                videoId, account, shouldFavorite, status, error)) {
            if (bitelog::g_logger) {
                ERR("video favorite change failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "收藏操作暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!status) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["favorited"] = status->favorited;
            setJsonResponse(response, 200, body);
        }
    };

    server.Post("/videos/favorite",
                 [changeFavorite](const httplib::Request& request,
                                  httplib::Response& response) {
                     changeFavorite(request, response, true);
                 });
    server.Post("/videos/unfavorite",
                 [changeFavorite](const httplib::Request& request,
                                  httplib::Response& response) {
                     changeFavorite(request, response, false);
                 });

    server.Get("/users/favorites", [context](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        const std::string claimedAccount = request.has_param("account")
            ? request.get_param_value("account") : "";
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后查看收藏";
            setJsonResponse(response, 200, body);
            return;
        }

        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!context.repositories.interactions->favoriteVideos(account, videos, error)) {
            if (bitelog::g_logger) {
                ERR("GET /users/favorites failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "我的收藏暂时不可用";
            setJsonResponse(response, 500, body);
            return;
        }

        body["success"] = true;
        body["videos"] = Json::arrayValue;
        for (const bitevideo::Video& video : videos) {
            body["videos"].append(bitevideo::toJson(video));
        }
        setJsonResponse(response, 200, body);
    });
    server.Get("/videos/comments", [context](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        std::string authenticatedAccount;
        if (!bindProtectedAccount(request, "", context.enforceGatewayIdentity,
                                  response, authenticatedAccount)) {
            return;
        }
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<std::vector<bitevideo::VideoComment>> comments;
        std::string error;
        if (!context.repositories.interactions->comments(videoId, comments, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/comments failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "评论列表暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!comments) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["comments"] = Json::arrayValue;
            for (const auto& comment : *comments) {
                body["comments"].append(commentToJson(comment));
            }
            setJsonResponse(response, 200, body);
        }
    });

    server.Post("/videos/comments", [context](const httplib::Request& request,
                                            httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        std::string userName = (*payload)["userName"].asString();
        const std::string claimedAccount = (*payload)["account"].asString();
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (!useAuthenticatedUserName(context.repositories, account,
                                      context.enforceGatewayIdentity, response,
                                      userName)) {
            return;
        }
        const std::string content = (*payload)["content"].asString();
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (account.empty() || userName.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后再发表评论";
            setJsonResponse(response, 200, body);
            return;
        }
        if (content.empty() || utf8CharCount(content) > 200) {
            body["success"] = false;
            body["message"] = "评论内容需为 1 到 200 个字符";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::VideoComment> comment;
        std::string error;
        if (!context.repositories.interactions->addComment(
                videoId, userName, account, content, comment, error)) {
            if (bitelog::g_logger) {
                ERR("POST /videos/comments failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "评论发送失败";
            setJsonResponse(response, 500, body);
        } else if (!comment) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "评论成功";
            body["comment"] = commentToJson(*comment);
            setJsonResponse(response, 200, body);
        }
    });

    server.Get("/videos/barrages", [context](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        std::string authenticatedAccount;
        if (!bindProtectedAccount(request, "", context.enforceGatewayIdentity,
                                  response, authenticatedAccount)) {
            return;
        }
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频标识不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<std::vector<bitevideo::VideoBarrage>> barrages;
        std::string error;
        if (!context.repositories.interactions->barrages(videoId, barrages, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/barrages failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "弹幕列表暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!barrages) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["barrages"] = Json::arrayValue;
            for (const auto& barrage : *barrages) {
                body["barrages"].append(barrageToJson(barrage));
            }
            setJsonResponse(response, 200, body);
        }
    });

    server.Post("/videos/barrages", [context](const httplib::Request& request,
                                            httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        std::string authenticatedAccount;
        if (!bindProtectedAccount(
                request, (*payload)["account"].asString(),
                context.enforceGatewayIdentity, response, authenticatedAccount)) {
            return;
        }

        const std::string videoId = (*payload)["videoId"].asString();
        const std::string text = (*payload)["text"].asString();
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频标识不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!(*payload)["seconds"].isInt() || (*payload)["seconds"].asInt() < 0) {
            body["success"] = false;
            body["message"] = "弹幕时间非法";
            setJsonResponse(response, 200, body);
            return;
        }
        if (text.empty()) {
            body["success"] = false;
            body["message"] = "弹幕内容不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string clippedText = utf8CharCount(text) > 30
            ? utf8Prefix(text, 30) : text;
        std::optional<bitevideo::VideoBarrage> barrage;
        std::string error;
        if (!context.repositories.interactions->addBarrage(
                videoId, (*payload)["seconds"].asInt(), clippedText,
                barrage, error)) {
            if (bitelog::g_logger) {
                ERR("POST /videos/barrages failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "弹幕发送失败";
            setJsonResponse(response, 500, body);
        } else if (!barrage) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "发送成功";
            body["seconds"] = barrage->seconds;
            body["text"] = barrage->text;
            setJsonResponse(response, 200, body);
        }
    });
}
}  // namespace biteserver
