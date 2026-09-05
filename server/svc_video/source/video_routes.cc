#include "video_routes.h"
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
void registerVideoRoutes(httplib::Server& server, RouteContext context) {
    if (!context.repositories.videos) return;
    server.Get("/videos", [context](const httplib::Request&,
                                  httplib::Response& response) {
        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!context.repositories.videos->list(videos, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos failed: {}", error);
            }
            Json::Value body;
            body["success"] = false;
            body["message"] = "视频列表暂时不可用";
            response.status = 500;
            response.set_content(
                biteutil::JSON::serialize(body).value_or(
                    R"({"success":false,"message":"database error"})"),
                "application/json; charset=utf-8");
            return;
        }

        Json::Value body(Json::arrayValue);
        for (const bitevideo::Video& video : videos) {
            body.append(bitevideo::toJson(video));
        }
        response.status = 200;
        response.set_content(
            biteutil::JSON::serialize(body).value_or("[]"),
            "application/json; charset=utf-8");
    });

    server.Post("/videos", [context](const httplib::Request& request,
                                   httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        bitevideo::VideoDraft draft;
        draftFromJson(*payload, draft);
        std::string authenticatedAccount;
        if (!bindProtectedAccount(request, draft.account,
                                  context.enforceGatewayIdentity, response,
                                  authenticatedAccount)) {
            return;
        }
        draft.account = authenticatedAccount;
        if (!useAuthenticatedUserName(context.repositories, draft.account,
                                      context.enforceGatewayIdentity, response,
                                      draft.userName)) {
            return;
        }

        if (draft.title.empty()) {
            body["success"] = false;
            body["message"] = "视频标题不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (draft.account.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后再发布";
            setJsonResponse(response, 200, body);
            return;
        }
        if (draft.category.empty()) {
            body["success"] = false;
            body["message"] = "请选择视频分类";
            setJsonResponse(response, 200, body);
            return;
        }
        if (draft.videoFileName.empty()) {
            body["success"] = false;
            body["message"] = "请先选择视频文件";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::Video> video;
        std::string error;
        if (!context.repositories.videos->createVideo(draft, video, error)) {
            if (bitelog::g_logger) {
                ERR("POST /videos failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "视频发布失败";
            setJsonResponse(response, 500, body);
        } else if (!video) {
            body["success"] = false;
            body["message"] = "视频发布失败";
            setJsonResponse(response, 500, body);
        } else {
            Json::Value videoJson = bitevideo::toJson(*video);
            videoJson["ownerAccount"] = draft.account;
            videoJson["videoFileName"] = draft.videoFileName;
            videoJson["coverFileName"] = draft.coverFileName;
            body["success"] = true;
            body["message"] = "发布成功";
            body["video"] = videoJson;
            setJsonResponse(response, 200, body);
        }
    });

    server.Post("/videos/upload", [context](const httplib::Request& request,
                                          httplib::Response& response) {
        Json::Value body;
        constexpr std::size_t MAX_VIDEO_BYTES = 64 * 1024 * 1024;
        constexpr std::size_t MAX_COVER_BYTES = 10 * 1024 * 1024;
        if (!request.is_multipart_form_data()) {
            body["success"] = false;
            body["message"] = "上传格式必须是 multipart/form-data";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!request.has_file("metadata") || !request.has_file("videoFile")) {
            body["success"] = false;
            body["message"] = "视频文件不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        const auto metadataPart = request.get_file_value("metadata");
        const auto metadata = biteutil::JSON::unserialize(metadataPart.content);
        if (!metadata || !metadata->isObject()) {
            body["success"] = false;
            body["message"] = "视频元数据格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        bitevideo::VideoDraft draft;
        if (!draftFromJson(*metadata, draft)) {
            body["success"] = false;
            body["message"] = "视频元数据格式错误";
            setJsonResponse(response, 200, body);
            return;
        }
        std::string authenticatedAccount;
        if (!bindProtectedAccount(request, draft.account,
                                  context.enforceGatewayIdentity, response,
                                  authenticatedAccount)) {
            return;
        }
        draft.account = authenticatedAccount;
        if (!useAuthenticatedUserName(context.repositories, draft.account,
                                      context.enforceGatewayIdentity, response,
                                      draft.userName)) {
            return;
        }
        const auto videoPart = request.get_file_value("videoFile");
        const std::string originalVideoName = pathFileName(videoPart.filename);
        if (!draft.videoFileName.empty()) {
            draft.videoFileName = pathFileName(draft.videoFileName);
        }
        if (draft.videoFileName.empty()) {
            draft.videoFileName = originalVideoName;
        }
        if (draft.title.empty() || draft.account.empty() ||
            draft.category.empty()) {
            body["success"] = false;
            body["message"] = "标题、账号和分类不能为空";
            setJsonResponse(response, 200, body);
            return;
        }
        if (originalVideoName.empty() || videoPart.content.empty() ||
            videoPart.content.size() > MAX_VIDEO_BYTES ||
            !hasAllowedVideoSuffix(originalVideoName)) {
            body["success"] = false;
            body["message"] = "视频文件不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::string uploadToken;
        std::string error;
        if (!bitesession::generateSessionToken(uploadToken, error)) {
            if (bitelog::g_logger) {
                ERR("upload path generation failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "视频文件保存失败";
            setJsonResponse(response, 500, body);
            return;
        }
        const std::string uploadPrefix = safeAccountName(draft.account) +
            "-" + uploadToken.substr(4, 32) + "-";
        const std::filesystem::path videoPath =
            std::filesystem::path("uploads") / "videos" /
            (uploadPrefix + originalVideoName);
        auto removeUploadedVideo = [&videoPath]() {
            std::error_code ignored;
            std::filesystem::remove(videoPath, ignored);
        };
        if (!writeBinaryFile(videoPath, videoPart.content, error)) {
            if (bitelog::g_logger) {
                ERR("video file write failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "视频文件保存失败";
            setJsonResponse(response, 500, body);
            return;
        }

        std::string storedCoverPath;
        if (request.has_file("coverFile")) {
            const auto coverPart = request.get_file_value("coverFile");
            const std::string coverName = pathFileName(coverPart.filename);
            if (!coverName.empty() && !coverPart.content.empty()) {
                if (coverPart.content.size() > MAX_COVER_BYTES ||
                    !hasAllowedAvatarSuffix(coverName)) {
                    removeUploadedVideo();
                    body["success"] = false;
                    body["message"] = "封面文件格式错误";
                    setJsonResponse(response, 200, body);
                    return;
                }
                if (draft.coverFileName.empty()) {
                    draft.coverFileName = coverName;
                } else {
                    draft.coverFileName = pathFileName(draft.coverFileName);
                }
                const std::filesystem::path coverPath =
                    std::filesystem::path("uploads") / "covers" /
                    (uploadPrefix + coverName);
                if (!writeBinaryFile(coverPath, coverPart.content, error)) {
                    removeUploadedVideo();
                    if (bitelog::g_logger) {
                        ERR("cover file write failed: {}", error);
                    }
                    body["success"] = false;
                    body["message"] = "封面文件保存失败";
                    setJsonResponse(response, 500, body);
                    return;
                }
                storedCoverPath = coverPath.generic_string();
            }
        }

        draft.playUrl = videoPath.generic_string();
        std::optional<bitevideo::Video> video;
        if (!context.repositories.videos->createVideo(draft, video, error)) {
            removeUploadedVideo();
            if (!storedCoverPath.empty()) {
                std::error_code ignored;
                std::filesystem::remove(storedCoverPath, ignored);
            }
            if (bitelog::g_logger) {
                ERR("POST /videos/upload failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "文件上传失败";
            setJsonResponse(response, 500, body);
        } else if (!video) {
            removeUploadedVideo();
            if (!storedCoverPath.empty()) {
                std::error_code ignored;
                std::filesystem::remove(storedCoverPath, ignored);
            }
            body["success"] = false;
            body["message"] = "文件上传失败";
            setJsonResponse(response, 500, body);
        } else {
            Json::Value videoJson = bitevideo::toJson(*video);
            videoJson["ownerAccount"] = draft.account;
            videoJson["videoFileName"] = draft.videoFileName;
            videoJson["coverFileName"] = draft.coverFileName;
            videoJson["storedVideoPath"] = videoPath.generic_string();
            videoJson["storedCoverPath"] = storedCoverPath;
            videoJson["playUrl"] = publicUploadUrl(videoPath.generic_string());
            body["success"] = true;
            body["message"] = "文件上传成功";
            body["video"] = videoJson;
            setJsonResponse(response, 200, body);
        }
    });
    server.Get("/videos/detail", [context](const httplib::Request& request,
                                         httplib::Response& response) {
        Json::Value body;
        const std::string videoId =
            request.has_param("id") ? request.get_param_value("id") : "";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
        } else {
            std::optional<bitevideo::Video> video;
            std::string error;
            if (!context.repositories.videos->findById(videoId, video, error)) {
                if (bitelog::g_logger) {
                    ERR("GET /videos/detail failed: {}", error);
                }
                response.status = 500;
                body["success"] = false;
                body["message"] = "视频详情暂时不可用";
            } else if (!video) {
                body["success"] = false;
                body["message"] = "视频不存在";
            } else {
                body["success"] = true;
                body["video"] = bitevideo::toJson(*video);
            }
        }

        if (response.status == -1) {
            response.status = 200;
        }
        response.set_content(
            biteutil::JSON::serialize(body).value_or(
                R"({"success":false,"message":"serialization error"})"),
            "application/json; charset=utf-8");
    });

    server.Get("/videos/search", [context](const httplib::Request& request,
                                         httplib::Response& response) {
        Json::Value body;
        const std::string keyword = request.has_param("keyword")
            ? request.get_param_value("keyword") : "";
        if (keyword.empty()) {
            body["success"] = false;
            body["message"] = "搜索关键词不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!context.repositories.videos->search(keyword, videos, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/search failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "视频搜索暂时不可用";
            setJsonResponse(response,
                            error.rfind("elasticsearch unavailable:", 0) == 0
                                ? 503
                                : 500,
                            body);
            return;
        }

        body["success"] = true;
        body["videos"] = Json::arrayValue;
        for (const bitevideo::Video& video : videos) {
            body["videos"].append(bitevideo::toJson(video));
        }
        setJsonResponse(response, 200, body);
    });

    server.Get("/videos/play-url", [context](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<std::string> playUrl;
        std::string error;
        if (!context.repositories.videos->playUrl(videoId, playUrl, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/play-url failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "播放地址暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!playUrl) {
            body["success"] = false;
            body["message"] = "视频不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["videoId"] = videoId;
            body["playUrl"] = publicUploadUrl(*playUrl);
            setJsonResponse(response, 200, body);
        }
    });
    server.Get("/users/videos", [context](const httplib::Request& request,
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
            body["message"] = "请先登录后查看作品";
            setJsonResponse(response, 200, body);
            return;
        }

        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!context.repositories.videos->ownerVideos(account, videos, error)) {
            if (bitelog::g_logger) {
                ERR("GET /users/videos failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "我的视频暂时不可用";
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
    if (context.repositories.admins) {
    server.Get("/admin/reviews", [context](const httplib::Request& request,
                                         httplib::Response& response) {
        if (!requireAdministrator(request, context.enforceGatewayIdentity,
                                  context.repositories.admins, response)) {
            return;
        }
        Json::Value body;
        std::vector<bitevideo::AdminReview> reviews;
        std::string error;
        if (!context.repositories.admins->adminReviews(reviews, error)) {
            if (bitelog::g_logger) {
                ERR("GET /admin/reviews failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "审核列表暂时不可用";
            setJsonResponse(response, 500, body);
            return;
        }

        body["success"] = true;
        body["reviews"] = Json::arrayValue;
        for (const auto& review : reviews) {
            body["reviews"].append(adminReviewToJson(review));
        }
        setJsonResponse(response, 200, body);
    });

    server.Post("/admin/reviews/action",
                 [context](const httplib::Request& request,
                        httplib::Response& response) {
        if (!requireAdministrator(request, context.enforceGatewayIdentity,
                                  context.repositories.admins, response)) {
            return;
        }
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        bool updated = false;
        std::string error;
        const std::string videoId = trimCopy((*payload)["videoId"].asString());
        const std::string status = trimCopy((*payload)["status"].asString());
        if (!context.repositories.admins->updateReviewStatus(videoId, status, updated, error)) {
            if (bitelog::g_logger) {
                ERR("POST /admin/reviews/action failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "审核状态更新失败";
            setJsonResponse(response, 500, body);
        } else if (!updated) {
            body["success"] = false;
            body["message"] = error.empty() ? "审核参数错误" : error;
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "审核状态已更新";
            setJsonResponse(response, 200, body);
        }
    });
    }
}
}  // namespace biteserver
