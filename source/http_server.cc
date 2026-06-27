#include "http_server.h"

#include "bitelog.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace biteserver {
namespace {

void setJsonResponse(httplib::Response& response,
                     int status,
                     const Json::Value& body) {
    response.status = status;
    response.set_content(
        biteutil::JSON::serialize(body).value_or(
            R"({"success":false,"message":"serialization error"})"),
        "application/json; charset=utf-8");
}

std::string accountOrGuest(const httplib::Request& request) {
    return request.has_param("account") &&
        !request.get_param_value("account").empty()
        ? request.get_param_value("account") : "guest";
}

std::string accountOrGuest(const Json::Value& payload) {
    const std::string account = payload["account"].asString();
    return account.empty() ? "guest" : account;
}

std::size_t utf8CharCount(const std::string& value) {
    std::size_t count = 0;
    for (unsigned char ch : value) {
        if ((ch & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

std::string utf8Prefix(const std::string& value, std::size_t maxChars) {
    std::size_t chars = 0;
    std::size_t bytes = 0;
    while (bytes < value.size() && chars < maxChars) {
        const unsigned char ch = static_cast<unsigned char>(value[bytes]);
        std::size_t step = 1;
        if ((ch & 0x80) == 0) {
            step = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            step = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            step = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            step = 4;
        }
        if (bytes + step > value.size()) {
            break;
        }
        bytes += step;
        ++chars;
    }
    return value.substr(0, bytes);
}

Json::Value commentToJson(const bitevideo::VideoComment& comment) {
    Json::Value value;
    value["id"] = comment.id;
    value["videoId"] = comment.videoId;
    value["userName"] = comment.userName;
    value["account"] = comment.account;
    value["content"] = comment.content;
    value["createdAt"] = comment.createdAt;
    return value;
}

Json::Value barrageToJson(const bitevideo::VideoBarrage& barrage) {
    Json::Value value;
    value["seconds"] = barrage.seconds;
    value["text"] = barrage.text;
    return value;
}

Json::Value userProfileToJson(const bitevideo::UserProfile& profile) {
    Json::Value value;
    value["account"] = profile.account;
    value["userName"] = profile.userName;
    value["description"] = profile.description;
    value["avatarPath"] = profile.avatarPath;
    return value;
}

Json::Value adminReviewToJson(const bitevideo::AdminReview& review) {
    Json::Value value;
    value["videoId"] = review.videoId;
    value["title"] = review.title;
    value["userId"] = review.userId;
    value["status"] = review.status;
    value["uploadTime"] = review.uploadTime;
    return value;
}

Json::Value adminUserToJson(const bitevideo::AdminUser& user) {
    Json::Value value;
    value["account"] = user.account;
    value["userName"] = user.userName;
    value["role"] = user.role;
    value["status"] = user.status;
    value["createdAt"] = user.createdAt;
    return value;
}

std::string trimCopy(std::string value) {
    const auto notSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(),
                value.end());
    return value;
}

std::string pathFileName(const std::string& filename) {
    return std::filesystem::path(filename).filename().string();
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::string safeAccountName(std::string account) {
    for (char& ch : account) {
        const bool safe = std::isalnum(static_cast<unsigned char>(ch)) ||
            ch == '-' || ch == '_';
        if (!safe) {
            ch = '_';
        }
    }
    return account.empty() ? "unknown" : account;
}

bool hasAllowedAvatarSuffix(const std::string& filename) {
    const std::string suffix =
        lowerAscii(std::filesystem::path(filename).extension().string());
    return suffix == ".png" || suffix == ".jpg" || suffix == ".jpeg";
}

bool hasAllowedVideoSuffix(const std::string& filename) {
    const std::string suffix =
        lowerAscii(std::filesystem::path(filename).extension().string());
    return suffix == ".mp4" || suffix == ".mov" || suffix == ".mkv" ||
        suffix == ".avi" || suffix == ".webm";
}

bool writeBinaryFile(const std::filesystem::path& path,
                     const std::string& content,
                     std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "创建目录失败: " + ec.message();
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "打开文件失败: " + path.string();
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) {
        error = "写入文件失败: " + path.string();
        return false;
    }
    return true;
}

bool draftFromJson(const Json::Value& payload,
                   bitevideo::VideoDraft& draft) {
    if (!payload.isObject()) {
        return false;
    }
    draft.title = trimCopy(payload["title"].asString());
    draft.account = trimCopy(payload["account"].asString());
    draft.category = trimCopy(payload["category"].asString());
    draft.userName = trimCopy(payload["userName"].asString());
    draft.description = trimCopy(payload["description"].asString());
    draft.videoFileName = trimCopy(payload["videoFileName"].asString());
    draft.coverFileName = trimCopy(payload["coverFileName"].asString());
    if (draft.userName.empty()) {
        draft.userName = draft.account;
    }
    if (payload["tags"].isArray()) {
        for (const Json::Value& tag : payload["tags"]) {
            if (tag.isString() && !tag.asString().empty()) {
                draft.tags.push_back(tag.asString());
            }
        }
    }
    return true;
}

}  // namespace

HttpServer::HttpServer(bitevideo::VideoStore& videoStore)
    : videoStore_(videoStore) {
    registerRoutes();
}

void HttpServer::registerRoutes() {
    server_.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        Json::Value body;
        body["code"] = 0;
        body["message"] = "ok";
        body["data"]["status"] = "UP";

        const auto json = biteutil::JSON::serialize(body);
        if (!json) {
            response.status = 500;
            response.set_content(
                R"({"code":500,"message":"response serialization failed"})",
                "application/json");
            return;
        }

        response.status = 200;
        response.set_content(*json, "application/json");
    });

    server_.Get("/videos", [this](const httplib::Request&,
                                  httplib::Response& response) {
        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!videoStore_.list(videos, error)) {
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

    server_.Post("/videos", [this](const httplib::Request& request,
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
        if (!videoStore_.createVideo(draft, video, error)) {
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

    server_.Post("/videos/upload", [this](const httplib::Request& request,
                                          httplib::Response& response) {
        Json::Value body;
        constexpr std::size_t MAX_VIDEO_BYTES = 200 * 1024 * 1024;
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

        const std::string uploadPrefix =
            safeAccountName(draft.account) + "-" +
            std::to_string(std::time(nullptr)) + "-";
        const std::filesystem::path videoPath =
            std::filesystem::path("uploads") / "videos" /
            (uploadPrefix + originalVideoName);
        auto removeUploadedVideo = [&videoPath]() {
            std::error_code ignored;
            std::filesystem::remove(videoPath, ignored);
        };
        std::string error;
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
        if (!videoStore_.createVideo(draft, video, error)) {
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
            body["success"] = true;
            body["message"] = "文件上传成功";
            body["video"] = videoJson;
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/login", [this](const httplib::Request& request,
                                  httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string account = trimCopy((*payload)["account"].asString());
        const std::string password = (*payload)["password"].asString();
        std::optional<bitevideo::UserProfile> profile;
        std::string error;
        if (!videoStore_.passwordLogin(account, password, profile, error)) {
            if (bitelog::g_logger) {
                ERR("POST /login failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "登录暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!profile) {
            body["success"] = false;
            body["message"] = "账号或密码错误";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["userName"] = profile->userName;
            body["account"] = profile->account;
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/login/email-code",
                 [this](const httplib::Request& request,
                        httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        bitevideo::EmailCodeSession session;
        std::string error;
        const std::string email = trimCopy((*payload)["email"].asString());
        if (!videoStore_.createEmailCode(email, session, error)) {
            if (bitelog::g_logger) {
                ERR("POST /login/email-code failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "验证码发送失败";
            setJsonResponse(response, 500, body);
        } else if (session.authcodeId.empty()) {
            body["success"] = false;
            body["message"] = error.empty() ? "邮箱格式错误" : error;
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "验证码已发送";
            body["authcodeId"] = session.authcodeId;
            body["debugCode"] = session.debugCode;
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/login/email", [this](const httplib::Request& request,
                                        httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string email = trimCopy((*payload)["email"].asString());
        const std::string authcodeId =
            trimCopy((*payload)["authcodeId"].asString());
        const std::string authcode =
            trimCopy((*payload)["authcode"].asString());
        std::optional<bitevideo::UserProfile> profile;
        std::string error;
        if (!videoStore_.emailLogin(
                email, authcodeId, authcode, profile, error)) {
            if (bitelog::g_logger) {
                ERR("POST /login/email failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "邮箱验证码登录失败";
            setJsonResponse(response, 500, body);
        } else if (!profile) {
            body["success"] = false;
            body["message"] = "验证码错误或已失效";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["userName"] = profile->userName;
            body["account"] = profile->account;
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/logout", [this](const httplib::Request& request,
                                   httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        bool knownUser = false;
        std::string error;
        const std::string account = trimCopy((*payload)["account"].asString());
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "当前没有登录用户";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!videoStore_.logout(account, knownUser, error)) {
            if (bitelog::g_logger) {
                ERR("POST /logout failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "退出登录失败";
            setJsonResponse(response, 500, body);
        } else if (!knownUser) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "已退出登录";
            setJsonResponse(response, 200, body);
        }
    });

    server_.Get("/videos/detail", [this](const httplib::Request& request,
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
            if (!videoStore_.findById(videoId, video, error)) {
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

    server_.Get("/videos/search", [this](const httplib::Request& request,
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
        if (!videoStore_.search(keyword, videos, error)) {
            if (bitelog::g_logger) {
                ERR("GET /videos/search failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "视频搜索暂时不可用";
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

    server_.Get("/videos/play-url", [this](const httplib::Request& request,
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
        if (!videoStore_.playUrl(videoId, playUrl, error)) {
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
            body["playUrl"] = *playUrl;
            setJsonResponse(response, 200, body);
        }
    });

    server_.Get("/videos/like-status", [this](const httplib::Request& request,
                                              httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string account = request.has_param("account") &&
            !request.get_param_value("account").empty()
            ? request.get_param_value("account") : "guest";
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::LikeStatus> status;
        std::string error;
        if (!videoStore_.likeStatus(videoId, account, status, error)) {
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

    const auto changeLike = [this](const httplib::Request& request,
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
        std::string account = (*payload)["account"].asString();
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
        if (!videoStore_.setLiked(videoId, account, shouldLike, status, error)) {
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

    server_.Post("/videos/like",
                 [changeLike](const httplib::Request& request,
                              httplib::Response& response) {
                     changeLike(request, response, true);
                 });
    server_.Post("/videos/unlike",
                 [changeLike](const httplib::Request& request,
                              httplib::Response& response) {
                     changeLike(request, response, false);
                 });

    server_.Get("/videos/watch-progress",
                [this](const httplib::Request& request,
                       httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string account = accountOrGuest(request);
        if (videoId.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::WatchProgress> progress;
        std::string error;
        if (!videoStore_.watchProgress(videoId, account, progress, error)) {
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

    server_.Post("/videos/watch-progress",
                 [this](const httplib::Request& request,
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
        const std::string account = accountOrGuest(*payload);
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
        if (!videoStore_.saveWatchProgress(
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

    server_.Get("/videos/favorite-status",
                [this](const httplib::Request& request,
                       httplib::Response& response) {
        Json::Value body;
        const std::string videoId = request.has_param("videoId")
            ? request.get_param_value("videoId") : "";
        const std::string account = request.has_param("account")
            ? request.get_param_value("account") : "";
        if (videoId.empty() || account.empty()) {
            body["success"] = false;
            body["message"] = "视频 id 和账号不能为空";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::FavoriteStatus> status;
        std::string error;
        if (!videoStore_.favoriteStatus(videoId, account, status, error)) {
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

    const auto changeFavorite = [this](const httplib::Request& request,
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
        const std::string account = (*payload)["account"].asString();
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
        if (!videoStore_.setFavorited(
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

    server_.Post("/videos/favorite",
                 [changeFavorite](const httplib::Request& request,
                                  httplib::Response& response) {
                     changeFavorite(request, response, true);
                 });
    server_.Post("/videos/unfavorite",
                 [changeFavorite](const httplib::Request& request,
                                  httplib::Response& response) {
                     changeFavorite(request, response, false);
                 });

    server_.Get("/users/favorites", [this](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
        const std::string account = request.has_param("account")
            ? request.get_param_value("account") : "";
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后查看收藏";
            setJsonResponse(response, 200, body);
            return;
        }

        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!videoStore_.favoriteVideos(account, videos, error)) {
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

    server_.Get("/users/videos", [this](const httplib::Request& request,
                                        httplib::Response& response) {
        Json::Value body;
        const std::string account = request.has_param("account")
            ? request.get_param_value("account") : "";
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "请先登录后查看作品";
            setJsonResponse(response, 200, body);
            return;
        }

        std::vector<bitevideo::Video> videos;
        std::string error;
        if (!videoStore_.ownerVideos(account, videos, error)) {
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

    server_.Get("/videos/comments", [this](const httplib::Request& request,
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

        std::optional<std::vector<bitevideo::VideoComment>> comments;
        std::string error;
        if (!videoStore_.comments(videoId, comments, error)) {
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

    server_.Post("/videos/comments", [this](const httplib::Request& request,
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
        const std::string userName = (*payload)["userName"].asString();
        const std::string account = (*payload)["account"].asString();
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
        if (!videoStore_.addComment(
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

    server_.Get("/videos/barrages", [this](const httplib::Request& request,
                                           httplib::Response& response) {
        Json::Value body;
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
        if (!videoStore_.barrages(videoId, barrages, error)) {
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

    server_.Post("/videos/barrages", [this](const httplib::Request& request,
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
        if (!videoStore_.addBarrage(
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

    server_.Get("/users/profile", [this](const httplib::Request& request,
                                         httplib::Response& response) {
        Json::Value body;
        const std::string account = request.has_param("account")
            ? request.get_param_value("account") : "";
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::UserProfile> profile;
        std::string error;
        if (!videoStore_.userProfile(account, profile, error)) {
            if (bitelog::g_logger) {
                ERR("GET /users/profile failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "个人资料暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!profile) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["user"] = userProfileToJson(*profile);
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/users/profile", [this](const httplib::Request& request,
                                          httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string account = (*payload)["account"].asString();
        const std::string userName = (*payload)["userName"].asString();
        const std::string description = (*payload)["description"].asString();
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
            return;
        }
        if (userName.empty() || utf8CharCount(userName) > 20) {
            body["success"] = false;
            body["message"] = "昵称需为 1 到 20 个字符";
            setJsonResponse(response, 200, body);
            return;
        }
        if (utf8CharCount(description) > 100) {
            body["success"] = false;
            body["message"] = "个人简介不能超过 100 个字符";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::UserProfile> profile;
        std::string error;
        if (!videoStore_.updateUserProfile(
                account, userName, description, profile, error)) {
            if (bitelog::g_logger) {
                ERR("POST /users/profile failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "个人资料保存失败";
            setJsonResponse(response, 500, body);
        } else if (!profile) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "保存成功";
            body["user"] = userProfileToJson(*profile);
            setJsonResponse(response, 200, body);
        }
    });

    server_.Post("/users/avatar", [this](const httplib::Request& request,
                                         httplib::Response& response) {
        Json::Value body;
        constexpr std::size_t MAX_AVATAR_BYTES = 5 * 1024 * 1024;
        if (!request.is_multipart_form_data()) {
            body["success"] = false;
            body["message"] = "头像上传格式错误";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!request.has_file("account") || !request.has_file("avatarFile")) {
            body["success"] = false;
            body["message"] = "头像上传格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const auto accountPart = request.get_file_value("account");
        const auto avatarPart = request.get_file_value("avatarFile");
        const std::string account = trimCopy(accountPart.content);
        const std::string avatarName = pathFileName(avatarPart.filename);
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
            return;
        }
        if (avatarPart.content.empty() ||
            avatarPart.content.size() > MAX_AVATAR_BYTES ||
            !hasAllowedAvatarSuffix(avatarName)) {
            body["success"] = false;
            body["message"] = "请选择 PNG 或 JPG 头像";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::UserProfile> profile;
        std::string error;
        if (!videoStore_.userProfile(account, profile, error)) {
            if (bitelog::g_logger) {
                ERR("POST /users/avatar profile lookup failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "头像保存失败";
            setJsonResponse(response, 500, body);
            return;
        }
        if (!profile) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::filesystem::path avatarPath =
            std::filesystem::path("uploads") / "avatars" /
            (safeAccountName(account) + "-" + avatarName);
        if (!writeBinaryFile(avatarPath, avatarPart.content, error)) {
            if (bitelog::g_logger) {
                ERR("avatar file write failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "头像保存失败";
            setJsonResponse(response, 500, body);
            return;
        }

        bool updated = false;
        if (!videoStore_.updateAvatarPath(
                account, avatarPath.generic_string(), updated, error)) {
            if (bitelog::g_logger) {
                ERR("POST /users/avatar failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "头像保存失败";
            setJsonResponse(response, 500, body);
        } else if (!updated) {
            body["success"] = false;
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "头像上传成功";
            body["avatarPath"] = avatarPath.generic_string();
            setJsonResponse(response, 200, body);
        }
    });

    server_.Get("/admin/reviews", [this](const httplib::Request&,
                                         httplib::Response& response) {
        Json::Value body;
        std::vector<bitevideo::AdminReview> reviews;
        std::string error;
        if (!videoStore_.adminReviews(reviews, error)) {
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

    server_.Post("/admin/reviews/action",
                 [this](const httplib::Request& request,
                        httplib::Response& response) {
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
        if (!videoStore_.updateReviewStatus(videoId, status, updated, error)) {
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

    server_.Get("/admin/users", [this](const httplib::Request&,
                                      httplib::Response& response) {
        Json::Value body;
        std::vector<bitevideo::AdminUser> users;
        std::string error;
        if (!videoStore_.adminUsers(users, error)) {
            if (bitelog::g_logger) {
                ERR("GET /admin/users failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "用户列表暂时不可用";
            setJsonResponse(response, 500, body);
            return;
        }

        body["success"] = true;
        body["users"] = Json::arrayValue;
        for (const auto& user : users) {
            body["users"].append(adminUserToJson(user));
        }
        setJsonResponse(response, 200, body);
    });

    server_.Post("/admin/users/action",
                 [this](const httplib::Request& request,
                        httplib::Response& response) {
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
        const std::string account = trimCopy((*payload)["account"].asString());
        const std::string action = trimCopy((*payload)["action"].asString());
        if (!videoStore_.updateAdminUser(account, action, updated, error)) {
            if (bitelog::g_logger) {
                ERR("POST /admin/users/action failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "角色操作失败";
            setJsonResponse(response, 500, body);
        } else if (!updated) {
            body["success"] = false;
            body["message"] = error.empty() ? "角色操作不支持" : error;
            setJsonResponse(response, 200, body);
        } else {
            body["success"] = true;
            body["message"] = "角色操作成功";
            setJsonResponse(response, 200, body);
        }
    });
}

bool HttpServer::listen(const std::string& host, std::uint16_t port) {
    return server_.listen(host, port);
}

int HttpServer::bindToAnyPort(const std::string& host) {
    return server_.bind_to_any_port(host);
}

bool HttpServer::listenAfterBind() {
    return server_.listen_after_bind();
}

void HttpServer::stop() {
    server_.stop();
}

}  // namespace biteserver
