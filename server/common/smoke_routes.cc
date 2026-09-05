#include "route_support.h"
#include "auth.h"
#include "bitelog.h"
#include "email_verification.h"
#include "session_token.h"
#include "util.h"
#include <algorithm>
#include <filesystem>
#include <cstdlib>
namespace biteserver {
using namespace detail;
void registerSmokeRoutes(httplib::Server& server, RouteContext context) {
    if (!smokeCleanupEnabled() || !context.repositories.admins) return;
    if (context.repositories.admins) {
        server.Post("/__smoke-cleanup",
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

            std::string authenticatedAccount;
            if (!bindProtectedAccount(
                    request, (*payload)["account"].asString(),
                    context.enforceGatewayIdentity, response,
                    authenticatedAccount)) {
                return;
            }

            const std::string videoId =
                trimCopy((*payload)["videoId"].asString());
            const std::string videoTitle =
                trimCopy((*payload)["videoTitle"].asString());
            const std::string account =
                trimCopy((*payload)["account"].asString());
            const std::string previousAvatarPath =
                (*payload)["previousAvatarPath"].asString();
            if (videoId.empty() || videoTitle.empty() || account.empty()) {
                body["success"] = false;
                body["message"] = "清理参数不完整";
                setJsonResponse(response, 200, body);
                return;
            }

            std::string error;
            if (!context.repositories.admins->smokeCleanup(videoId, videoTitle, account,
                                          previousAvatarPath, error)) {
                body["success"] = false;
                body["message"] = "测试数据清理失败";
                if (bitelog::g_logger) {
                    ERR("POST /__smoke-cleanup database failed: {}", error);
                }
                setJsonResponse(response, 500, body);
                return;
            }

            const char* uploadFields[] = {
                "storedVideoPath", "storedCoverPath", "avatarPath"};
            for (const char* field : uploadFields) {
                if (!safeRemoveUploadPath((*payload)[field].asString(),
                                          error)) {
                    body["success"] = false;
                    body["message"] = error;
                    setJsonResponse(response, 500, body);
                    return;
                }
            }

            body["success"] = true;
            body["message"] = "smoke cleanup complete";
            setJsonResponse(response, 200, body);
        });
    }
}
}  // namespace biteserver
