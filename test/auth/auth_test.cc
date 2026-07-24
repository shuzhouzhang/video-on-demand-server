#include "../../server/common/auth.h"
#include "../../server/common/email_verification.h"
#include "../../server/common/password_hash.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "[FAIL] " << message << '\n'; return false; }
    std::cout << "[PASS] " << message << '\n'; return true;
}
}  // namespace

int main() {
    bool ok = true;
    const auto token = biteauth::parseBearerToken("Bearer vod-abc123");
    ok &= expect(token && *token == "vod-abc123", "Bearer returns pure token");
    ok &= expect(!biteauth::parseBearerToken("vod-abc123"), "bare token rejected");
    ok &= expect(!biteauth::parseBearerToken("Bearer "), "empty token rejected");
    ok &= expect(biteauth::redisSessionKeyForToken("vod-abc123") ==
                     "vod:session:vod-abc123", "Redis key has no Bearer prefix");

    std::string passwordHash;
    std::string secondPasswordHash;
    std::string cryptoError;
    ok &= expect(
        biteauth::hashPassword("correct horse battery staple", passwordHash,
                               cryptoError) &&
            biteauth::hashPassword("correct horse battery staple",
                                   secondPasswordHash, cryptoError) &&
            biteauth::isVersionedPasswordHash(passwordHash) &&
            passwordHash != secondPasswordHash &&
            passwordHash.find("correct horse battery staple") ==
                std::string::npos,
        "PBKDF2 hashes are versioned, salted, and never plaintext");
    biteauth::PasswordVerification passwordVerification;
    ok &= expect(
        biteauth::verifyPassword("correct horse battery staple", passwordHash,
                                 passwordVerification, cryptoError) &&
            passwordVerification.matched &&
            !passwordVerification.needsMigration,
        "versioned password verifies successfully");
    ok &= expect(
        biteauth::verifyPassword("wrong password", passwordHash,
                                 passwordVerification, cryptoError) &&
            !passwordVerification.matched,
        "wrong password does not verify");
    ok &= expect(
        biteauth::verifyPassword(
            "malformed", "$pbkdf2-sha256$v=1$i=210000$bad$bad",
            passwordVerification, cryptoError) &&
            !passwordVerification.matched &&
            !passwordVerification.needsMigration,
        "malformed versioned hash never falls back to plaintext");
    ok &= expect(
        biteauth::verifyPassword("123456", "123456", passwordVerification,
                                 cryptoError) &&
            passwordVerification.matched &&
            passwordVerification.needsMigration,
        "legacy plaintext login is marked for one-time migration");

    std::set<std::string> generatedCodes;
    bool codesValid = true;
    for (int index = 0; index < 20; ++index) {
        std::string generatedCode;
        codesValid &= biteauth::generateEmailCode(generatedCode, cryptoError) &&
            generatedCode.size() == 6;
        for (const unsigned char ch : generatedCode) {
            codesValid &= std::isdigit(ch) != 0;
        }
        generatedCodes.insert(generatedCode);
    }
    ok &= expect(codesValid && generatedCodes.size() > 1,
                 "email codes are random six-digit values");
    ok &= expect(biteauth::EMAIL_CODE_TTL_MINUTES == 10 &&
                     biteauth::EMAIL_CODE_MAX_ATTEMPTS == 5 &&
                     biteauth::EMAIL_CODE_SEND_INTERVAL_SECONDS == 60,
                 "email verification security limits are explicit");

#if defined(_WIN32)
    _putenv_s("VIDEO_ENABLE_EMAIL_DEBUG_CODE", "");
#else
    unsetenv("VIDEO_ENABLE_EMAIL_DEBUG_CODE");
#endif
    ok &= expect(!biteauth::emailDebugCodeEnabled(),
                 "email debug code is disabled by default");
#if defined(_WIN32)
    _putenv_s("VIDEO_ENABLE_EMAIL_DEBUG_CODE", "1");
#else
    setenv("VIDEO_ENABLE_EMAIL_DEBUG_CODE", "1", 1);
#endif
    ok &= expect(biteauth::emailDebugCodeEnabled(),
                 "email debug code requires explicit environment opt-in");
#if defined(_WIN32)
    _putenv_s("VIDEO_ENABLE_EMAIL_DEBUG_CODE", "");
#else
    unsetenv("VIDEO_ENABLE_EMAIL_DEBUG_CODE");
#endif

    httplib::Headers headers{{"Authorization", "Bearer vod-abc123"},
        {biteauth::AUTHENTICATED_ACCOUNT_HEADER, "attacker"},
        {biteauth::AUTHENTICATED_ROLE_HEADER, "管理员"},
        {biteauth::GATEWAY_VERIFIED_HEADER, "1"}};
    biteauth::applyGatewayIdentity(headers, std::string("real-user"));
    ok &= expect(headers.count(biteauth::AUTHENTICATED_ACCOUNT_HEADER) == 1 &&
                     headers.find(biteauth::AUTHENTICATED_ACCOUNT_HEADER)->second ==
                         "real-user" &&
                     headers.count(biteauth::AUTHENTICATED_ROLE_HEADER) == 0 &&
                     headers.count("Authorization") == 1,
                 "forged identity stripped and Redis account injected");

    std::string lookedUp;
    const auto authenticated = biteauth::authenticateGatewayRequest(
        true, true, "Bearer vod-real",
        [&lookedUp](const std::string& value, std::string&) {
            lookedUp = value; return std::optional<std::string>("user-a");
        });
    ok &= expect(authenticated.status == biteauth::GatewayAuthStatus::Allowed &&
                     authenticated.account == "user-a" && lookedUp == "vod-real",
                 "Redis token account propagates through gateway auth");

    const auto missingSession = biteauth::authenticateGatewayRequest(
        true, true, "Bearer deleted-token",
        [](const std::string&, std::string&) {
            return std::optional<std::string>{};
        });
    ok &= expect(
        missingSession.status == biteauth::GatewayAuthStatus::SessionAbsent,
        "well-formed token with deleted session is distinguishable");

    const auto redisFailure = biteauth::authenticateGatewayRequest(
        true, true, "Bearer vod-real",
        [](const std::string&, std::string& error) {
            error = "redis unavailable";
            return std::optional<std::string>{};
        });
    ok &= expect(redisFailure.status ==
                     biteauth::GatewayAuthStatus::DependencyUnavailable,
                 "Redis lookup failure is a dependency error");

    httplib::Request request;
    request.headers.emplace(biteauth::AUTHENTICATED_ACCOUNT_HEADER, "user-a");
    request.headers.emplace(biteauth::GATEWAY_VERIFIED_HEADER, "1");
    ok &= expect(
        biteauth::bindAuthenticatedAccount(request, "user-a", true).status ==
                biteauth::IdentityStatus::Allowed &&
            biteauth::bindAuthenticatedAccount(request, "", true).account ==
                "user-a" &&
            biteauth::bindAuthenticatedAccount(request, "user-b", true).status ==
                biteauth::IdentityStatus::Forbidden,
        "downstream binds legacy account to gateway identity");
    return ok ? 0 : 1;
}
