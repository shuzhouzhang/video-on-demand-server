#pragma once

#include <functional>
#include <optional>
#include <string>

#include <httplib.h>

namespace biteauth {

inline constexpr const char* AUTHENTICATED_ACCOUNT_HEADER =
    "X-Authenticated-Account";
inline constexpr const char* AUTHENTICATED_ROLE_HEADER =
    "X-Authenticated-Role";
inline constexpr const char* GATEWAY_VERIFIED_HEADER = "X-Gateway-Verified";

// Only "Bearer <non-empty-token>" is accepted. Bare tokens and tokens with
// whitespace are rejected; callers always receive the pure token.
std::optional<std::string> parseBearerToken(
    const std::string& authorizationValue);
std::string redisSessionKeyForToken(const std::string& token);

enum class GatewayAuthStatus {
    Allowed,
    Unauthorized,
    SessionAbsent,
    DependencyUnavailable
};

struct GatewayAuthResult {
    GatewayAuthStatus status = GatewayAuthStatus::Allowed;
    std::optional<std::string> account;
};

using TokenLookup = std::function<std::optional<std::string>(
    const std::string& token, std::string& error)>;

GatewayAuthResult authenticateGatewayRequest(
    bool authEnabled,
    bool routeRequiresAuth,
    const std::string& authorizationValue,
    const TokenLookup& lookup);

void stripUntrustedIdentityHeaders(httplib::Headers& headers);
void applyGatewayIdentity(httplib::Headers& headers,
                          const std::optional<std::string>& account);

enum class IdentityStatus { Allowed, Unauthenticated, Forbidden };

struct IdentityResult {
    IdentityStatus status = IdentityStatus::Unauthenticated;
    std::string account;
};

// Strict mode trusts only a gateway-verified account. A legacy account may be
// omitted or must match it. Demo mode preserves the legacy account unchanged.
IdentityResult bindAuthenticatedAccount(const httplib::Request& request,
                                        const std::string& claimedAccount,
                                        bool enforceGatewayIdentity);

bool isAdministratorRole(const std::string& role);

}  // namespace biteauth
