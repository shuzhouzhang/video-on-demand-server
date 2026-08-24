#include "auth.h"

#include <algorithm>
#include <cctype>

namespace biteauth {
namespace {

std::string trimCopy(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(),
                value.end());
    return value;
}

bool equalsAsciiIgnoreCase(const std::string& left,
                           const std::string& right) {
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(),
                   [](unsigned char lhs, unsigned char rhs) {
                       return std::tolower(lhs) == std::tolower(rhs);
                   });
}

}  // namespace

std::optional<std::string> parseBearerToken(
    const std::string& authorizationValue) {
    const std::string value = trimCopy(authorizationValue);
    const auto separator = value.find_first_of(" \t");
    if (separator == std::string::npos ||
        !equalsAsciiIgnoreCase(value.substr(0, separator), "Bearer")) {
        return std::nullopt;
    }
    const std::string token = trimCopy(value.substr(separator + 1));
    if (token.empty() ||
        std::any_of(token.begin(), token.end(), [](unsigned char ch) {
            return std::isspace(ch);
        })) {
        return std::nullopt;
    }
    return token;
}

std::string redisSessionKeyForToken(const std::string& token) {
    return "vod:session:" + token;
}

GatewayAuthResult authenticateGatewayRequest(
    bool authEnabled,
    bool routeRequiresAuth,
    const std::string& authorizationValue,
    const TokenLookup& lookup) {
    if (!routeRequiresAuth || !authEnabled) {
        return {};
    }
    const auto token = parseBearerToken(authorizationValue);
    if (!token) {
        return {GatewayAuthStatus::Unauthorized, std::nullopt};
    }
    std::string error;
    const auto account = lookup(*token, error);
    if (!error.empty()) {
        return {GatewayAuthStatus::DependencyUnavailable, std::nullopt};
    }
    if (!account || trimCopy(*account).empty()) {
        // A syntactically valid token without a Redis session is kept
        // distinct so logout can be idempotent without weakening other
        // protected routes.
        return {GatewayAuthStatus::SessionAbsent, std::nullopt};
    }
    return {GatewayAuthStatus::Allowed, trimCopy(*account)};
}

void stripUntrustedIdentityHeaders(httplib::Headers& headers) {
    headers.erase(AUTHENTICATED_ACCOUNT_HEADER);
    headers.erase(AUTHENTICATED_ROLE_HEADER);
    headers.erase(GATEWAY_VERIFIED_HEADER);
}

void applyGatewayIdentity(httplib::Headers& headers,
                          const std::optional<std::string>& account) {
    stripUntrustedIdentityHeaders(headers);
    if (account && !account->empty()) {
        headers.emplace(AUTHENTICATED_ACCOUNT_HEADER, *account);
        headers.emplace(GATEWAY_VERIFIED_HEADER, "1");
    }
}

IdentityResult bindAuthenticatedAccount(const httplib::Request& request,
                                        const std::string& claimedAccount,
                                        bool enforceGatewayIdentity) {
    const std::string claimed = trimCopy(claimedAccount);
    if (!enforceGatewayIdentity) {
        return {IdentityStatus::Allowed, claimed};
    }
    if (request.get_header_value(GATEWAY_VERIFIED_HEADER) != "1") {
        return {IdentityStatus::Unauthenticated, ""};
    }
    const std::string authenticated =
        trimCopy(request.get_header_value(AUTHENTICATED_ACCOUNT_HEADER));
    if (authenticated.empty()) {
        return {IdentityStatus::Unauthenticated, ""};
    }
    if (!claimed.empty() && claimed != authenticated) {
        return {IdentityStatus::Forbidden, ""};
    }
    return {IdentityStatus::Allowed, authenticated};
}

bool isAdministratorRole(const std::string& role) {
    return role == "管理员" || role == "超级管理员";
}

}  // namespace biteauth
