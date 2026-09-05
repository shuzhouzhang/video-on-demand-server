#include "route_support.h"

#include "auth.h"
#include "bitelog.h"
#include "email_verification.h"
#include "redis_session_manager.h"
#include "session_token.h"
#include "util.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <utility>

namespace biteserver::detail {
std::string trimCopy(std::string value);

void setJsonResponse(httplib::Response& response,
                     int status,
                     const Json::Value& body) {
    response.status = status;
    response.set_content(
        biteutil::JSON::serialize(body).value_or(
            R"({"success":false,"message":"serialization error"})"),
        "application/json; charset=utf-8");
}

bool bindProtectedAccount(const httplib::Request& request,
                          const std::string& claimedAccount,
                          bool enforceGatewayIdentity,
                          httplib::Response& response,
                          std::string& account) {
    const auto identity = biteauth::bindAuthenticatedAccount(
        request, trimCopy(claimedAccount), enforceGatewayIdentity);
    if (identity.status == biteauth::IdentityStatus::Allowed) {
        account = identity.account;
        return true;
    }
    Json::Value body;
    body["success"] = false;
    if (identity.status == biteauth::IdentityStatus::Forbidden) {
        body["message"] = "认证账号与请求账号不一致";
        setJsonResponse(response, 403, body);
    } else {
        body["message"] = "unauthorized";
        setJsonResponse(response, 401, body);
    }
    return false;
}

bool requireAdministrator(const httplib::Request& request,
                          bool enforceGatewayIdentity,
                          biterepo::IAdminRepository* adminRepository,
                          httplib::Response& response) {
    if (!enforceGatewayIdentity) {
        return true;
    }
    if (!adminRepository) {
        Json::Value body;
        body["success"] = false;
        body["message"] = "authorization unavailable";
        setJsonResponse(response, 500, body);
        return false;
    }
    std::string account;
    if (!bindProtectedAccount(request, "", true, response, account)) {
        return false;
    }
    std::optional<bitevideo::UserAccess> access;
    std::string error;
    if (!adminRepository->userAccess(account, access, error)) {
        if (bitelog::g_logger) {
            ERR("administrator access lookup failed: {}", error);
        }
        Json::Value body;
        body["success"] = false;
        body["message"] = "authorization unavailable";
        setJsonResponse(response, 500, body);
        return false;
    }
    if (!access || access->status != "启用" ||
        !biteauth::isAdministratorRole(access->role)) {
        Json::Value body;
        body["success"] = false;
        body["message"] = "forbidden";
        setJsonResponse(response, 403, body);
        return false;
    }
    return true;
}

bool useAuthenticatedUserName(const biterepo::RepositorySet& repositories,
                              const std::string& account,
                              bool enforceGatewayIdentity,
                              httplib::Response& response,
                              std::string& userName) {
    if (!enforceGatewayIdentity) {
        return true;
    }
    std::optional<bitevideo::UserProfile> profile;
    std::string error;
    bool loaded = false;
    if (repositories.users) {
        loaded = repositories.users->userProfile(account, profile, error);
    } else if (repositories.interactions) {
        loaded = repositories.interactions->interactionUserProfile(
            account, profile, error);
    }
    if (!loaded) {
        Json::Value body;
        body["success"] = false;
        body["message"] = "用户资料暂时不可用";
        setJsonResponse(response, 500, body);
        return false;
    }
    if (!profile) {
        Json::Value body;
        body["success"] = false;
        body["message"] = "forbidden";
        setJsonResponse(response, 403, body);
        return false;
    }
    userName = profile->userName;
    return true;
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

std::string publicUploadUrl(const std::string& storedPath) {
    std::string normalized = storedPath;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.rfind("uploads/", 0) == 0) {
        return "/" + normalized;
    }
    return normalized;
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
    // "xb" atomically creates a new file and refuses an existing path, so a
    // collision can never silently truncate an earlier upload.
    std::FILE* out = std::fopen(path.string().c_str(), "wbx");
    if (!out) {
        error = "打开文件失败: " + std::string(std::strerror(errno));
        return false;
    }
    const std::size_t written =
        std::fwrite(content.data(), 1, content.size(), out);
    const bool closed = std::fclose(out) == 0;
    if (written != content.size() || !closed) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        error = "写入文件失败: " + path.string();
        return false;
    }
    return true;
}

bool smokeCleanupEnabled() {
    const char* value = std::getenv("VIDEO_ENABLE_SMOKE_CLEANUP");
    return value != nullptr && std::string(value) == "1";
}

bool safeRemoveUploadPath(const std::string& storedPath, std::string& error) {
    if (storedPath.empty()) {
        return true;
    }

    std::string normalized = storedPath;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.rfind("/uploads/", 0) == 0) {
        normalized.erase(0, 1);
    }
    if (normalized.rfind("uploads/", 0) != 0) {
        error = "只能清理uploads目录下的测试文件";
        return false;
    }

    const std::filesystem::path relative(normalized);
    if (relative.is_absolute()) {
        error = "不能清理绝对路径";
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            error = "不能清理上级目录路径";
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::remove(relative, ec);
    if (ec) {
        error = "清理测试文件失败: " + ec.message();
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

}  // namespace biteserver::detail
