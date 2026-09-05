#pragma once
#include "../repository/repository.h"
#include "redis_session_manager.h"
#include <filesystem>
#include <httplib.h>
#include <jsoncpp/json/json.h>
namespace biteserver {
// 注册时复制指针和策略；Repository 与 Session 必须比 HTTP 服务活得更久。
struct RouteContext {
    biterepo::RepositorySet repositories;
    bitesession::RedisSessionManager* sessions = nullptr;
    bool enforceGatewayIdentity = false;
};
namespace detail {
void setJsonResponse(httplib::Response& response,
                     int status,
                     const Json::Value& body);
bool bindProtectedAccount(const httplib::Request& request,
                          const std::string& claimedAccount,
                          bool enforceGatewayIdentity,
                          httplib::Response& response,
                          std::string& account);
bool requireAdministrator(const httplib::Request& request,
                          bool enforceGatewayIdentity,
                          biterepo::IAdminRepository* adminRepository,
                          httplib::Response& response);
bool useAuthenticatedUserName(const biterepo::RepositorySet& repositories,
                              const std::string& account,
                              bool enforceGatewayIdentity,
                              httplib::Response& response,
                              std::string& userName);
std::size_t utf8CharCount(const std::string& value);
std::string utf8Prefix(const std::string& value, std::size_t maxChars);
Json::Value commentToJson(const bitevideo::VideoComment& comment);
Json::Value barrageToJson(const bitevideo::VideoBarrage& barrage);
Json::Value userProfileToJson(const bitevideo::UserProfile& profile);
Json::Value adminReviewToJson(const bitevideo::AdminReview& review);
Json::Value adminUserToJson(const bitevideo::AdminUser& user);
std::string trimCopy(std::string value);
std::string pathFileName(const std::string& filename);
std::string lowerAscii(std::string value);
std::string safeAccountName(std::string account);
bool hasAllowedAvatarSuffix(const std::string& filename);
bool hasAllowedVideoSuffix(const std::string& filename);
std::string publicUploadUrl(const std::string& storedPath);
bool writeBinaryFile(const std::filesystem::path& path,
                     const std::string& content,
                     std::string& error);
bool smokeCleanupEnabled();
bool safeRemoveUploadPath(const std::string& storedPath, std::string& error);
bool draftFromJson(const Json::Value& payload,
                   bitevideo::VideoDraft& draft);
}  // namespace detail
void registerSmokeRoutes(httplib::Server& server, RouteContext context);
}  // namespace biteserver
