#include "user_routes.h"
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
void registerUserRoutes(httplib::Server& server, RouteContext context) {
    if (!context.repositories.users) return;
    server.Post("/login", [context](const httplib::Request& request,
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
        if (!context.repositories.users->passwordLogin(account, password, profile, error)) {
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
            std::string token;
            if (context.sessions &&
                !context.sessions->createToken(profile->account, token, error)) {
                if (bitelog::g_logger) {
                    ERR("POST /login redis session failed: {}", error);
                }
                body["success"] = false;
                body["message"] = "登录状态保存失败";
                setJsonResponse(response, 500, body);
                return;
            }
            body["success"] = true;
            body["userName"] = profile->userName;
            body["account"] = profile->account;
            if (!token.empty()) {
                body["token"] = token;
            }
            setJsonResponse(response, 200, body);
        }
    });
    server.Post("/login/password", [context](const httplib::Request& request,
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
        if (!context.repositories.users->passwordLogin(account, password, profile, error)) {
            if (bitelog::g_logger) {
                ERR("POST /login/password failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "登录暂时不可用";
            setJsonResponse(response, 500, body);
        } else if (!profile) {
            body["success"] = false;
            body["message"] = "账号或密码错误";
            setJsonResponse(response, 200, body);
        } else {
            std::string token;
            if (context.sessions &&
                !context.sessions->createToken(profile->account, token, error)) {
                if (bitelog::g_logger) {
                    ERR("POST /login/password redis session failed: {}", error);
                }
                body["success"] = false;
                body["message"] = "登录状态保存失败";
                setJsonResponse(response, 500, body);
                return;
            }
            body["success"] = true;
            body["userName"] = profile->userName;
            body["account"] = profile->account;
            if (!token.empty()) {
                body["token"] = token;
            }
            setJsonResponse(response, 200, body);
        }
    });

    server.Post("/login/email-code",
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

        bitevideo::EmailCodeSession session;
        std::string error;
        const std::string email = trimCopy((*payload)["email"].asString());
        if (!context.repositories.users->createEmailCode(email, session, error)) {
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
            if (biteauth::emailDebugCodeEnabled() &&
                !session.debugCode.empty()) {
                body["debugCode"] = session.debugCode;
            }
            setJsonResponse(response, 200, body);
        }
    });

    server.Post("/login/email", [context](const httplib::Request& request,
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
        if (!context.repositories.users->emailLogin(
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
            std::string token;
            if (context.sessions &&
                !context.sessions->createToken(profile->account, token, error)) {
                if (bitelog::g_logger) {
                    ERR("POST /login/email redis session failed: {}", error);
                }
                body["success"] = false;
                body["message"] = "登录状态保存失败";
                setJsonResponse(response, 500, body);
                return;
            }
            body["success"] = true;
            body["userName"] = profile->userName;
            body["account"] = profile->account;
            if (!token.empty()) {
                body["token"] = token;
            }
            setJsonResponse(response, 200, body);
        }
    });

    server.Post("/logout", [context](const httplib::Request& request,
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
        std::string account;
        if (!bindProtectedAccount(
                request, (*payload)["account"].asString(),
                context.enforceGatewayIdentity, response, account)) {
            return;
        }
        if (account.empty()) {
            body["success"] = false;
            body["message"] = "当前没有登录用户";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!context.repositories.users->logout(account, knownUser, error)) {
            if (bitelog::g_logger) {
                ERR("POST /logout failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "退出登录失败";
            setJsonResponse(response, 500, body);
        } else {
            const auto token = biteauth::parseBearerToken(
                request.get_header_value("Authorization"));
            if (context.enforceGatewayIdentity && !token) {
                body["success"] = false;
                body["message"] = "unauthorized";
                setJsonResponse(response, 401, body);
                return;
            }
            if (context.sessions && token &&
                !context.sessions->deleteToken(*token, error)) {
                if (bitelog::g_logger) {
                    ERR("POST /logout redis session delete failed: {}", error);
                }
                body["success"] = false;
                body["message"] = "退出登录暂时不可用";
                setJsonResponse(response, 503, body);
                return;
            }
            body["success"] = true;
            body["message"] = "已退出登录";
            setJsonResponse(response, 200, body);
        }
    });
    server.Get("/users/profile", [context](const httplib::Request& request,
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
            body["message"] = "用户不存在";
            setJsonResponse(response, 200, body);
            return;
        }

        std::optional<bitevideo::UserProfile> profile;
        std::string error;
        if (!context.repositories.users->userProfile(account, profile, error)) {
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

    server.Post("/users/profile", [context](const httplib::Request& request,
                                          httplib::Response& response) {
        Json::Value body;
        const auto payload = biteutil::JSON::unserialize(request.body);
        if (!payload || !payload->isObject()) {
            body["success"] = false;
            body["message"] = "请求JSON格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const std::string claimedAccount = (*payload)["account"].asString();
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
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
        if (!context.repositories.users->updateUserProfile(
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

    server.Post("/users/avatar", [context](const httplib::Request& request,
                                         httplib::Response& response) {
        Json::Value body;
        constexpr std::size_t MAX_AVATAR_BYTES = 5 * 1024 * 1024;
        if (!request.is_multipart_form_data()) {
            body["success"] = false;
            body["message"] = "头像上传格式错误";
            setJsonResponse(response, 200, body);
            return;
        }
        if (!request.has_file("avatarFile") ||
            (!context.enforceGatewayIdentity && !request.has_file("account"))) {
            body["success"] = false;
            body["message"] = "头像上传格式错误";
            setJsonResponse(response, 200, body);
            return;
        }

        const auto avatarPart = request.get_file_value("avatarFile");
        const std::string claimedAccount = request.has_file("account")
            ? request.get_file_value("account").content : "";
        std::string account;
        if (!bindProtectedAccount(request, claimedAccount,
                                  context.enforceGatewayIdentity, response, account)) {
            return;
        }
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
        if (!context.repositories.users->userProfile(account, profile, error)) {
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

        std::string avatarToken;
        if (!bitesession::generateSessionToken(avatarToken, error)) {
            if (bitelog::g_logger) {
                ERR("avatar path generation failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "头像保存失败";
            setJsonResponse(response, 500, body);
            return;
        }
        const std::filesystem::path avatarPath =
            std::filesystem::path("uploads") / "avatars" /
            (safeAccountName(account) + "-" + avatarToken.substr(4, 32) +
             "-" + avatarName);
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
        if (!context.repositories.users->updateAvatarPath(
                account, avatarPath.generic_string(), updated, error)) {
            std::error_code ignored;
            std::filesystem::remove(avatarPath, ignored);
            if (bitelog::g_logger) {
                ERR("POST /users/avatar failed: {}", error);
            }
            body["success"] = false;
            body["message"] = "头像保存失败";
            setJsonResponse(response, 500, body);
        } else if (!updated) {
            std::error_code ignored;
            std::filesystem::remove(avatarPath, ignored);
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
    if (context.repositories.admins) {
    server.Get("/admin/users", [context](const httplib::Request& request,
                                      httplib::Response& response) {
        if (!requireAdministrator(request, context.enforceGatewayIdentity,
                                  context.repositories.admins, response)) {
            return;
        }
        Json::Value body;
        std::vector<bitevideo::AdminUser> users;
        std::string error;
        if (!context.repositories.admins->adminUsers(users, error)) {
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

    server.Post("/admin/users/action",
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
        const std::string account = trimCopy((*payload)["account"].asString());
        const std::string action = trimCopy((*payload)["action"].asString());
        if (!context.repositories.admins->updateAdminUser(account, action, updated, error)) {
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
}
}  // namespace biteserver
