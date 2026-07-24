#include "user_repository.h"

#include "../../common/email_verification.h"
#include "../../common/password_hash.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace biteuser {
namespace {

std::string valueOrEmpty(const std::optional<std::string>& value) {
    return value.value_or("");
}

bool profileFromRow(const bitedb::Database::QueryRow& row,
                    std::size_t offset,
                    bitevideo::UserProfile& profile,
                    std::string& error) {
    if (row.size() < offset + 4) {
        error = "用户资料查询返回了不符合预期的字段数量";
        return false;
    }
    profile.account = valueOrEmpty(row[offset]);
    profile.userName = valueOrEmpty(row[offset + 1]);
    profile.description = valueOrEmpty(row[offset + 2]);
    profile.avatarPath = valueOrEmpty(row[offset + 3]);
    return true;
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::string nameFromEmail(const std::string& email) {
    const auto at = email.find('@');
    return at == std::string::npos || at == 0 ? email : email.substr(0, at);
}

bool isSixDigitCode(const std::string& code) {
    return code.size() == 6 &&
        std::all_of(code.begin(), code.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        });
}

}  // namespace

MySqlUserRepository::MySqlUserRepository(bitedb::Database& database)
    : database_(database) {}

bool MySqlUserRepository::userProfile(
    const std::string& account,
    std::optional<bitevideo::UserProfile>& profile,
    std::string& error) {
    profile.reset();
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) return false;
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(
            "SELECT account, user_name, description, avatar_path FROM users "
            "WHERE account = '" + escapedAccount + "' LIMIT 1",
            rows, error)) {
        return false;
    }
    if (rows.empty()) return true;
    bitevideo::UserProfile found;
    if (!profileFromRow(rows.front(), 0, found, error)) return false;
    profile = std::move(found);
    return true;
}

bool MySqlUserRepository::updateUserProfile(
    const std::string& account,
    const std::string& userName,
    const std::string& description,
    std::optional<bitevideo::UserProfile>& profile,
    std::string& error) {
    if (!userProfile(account, profile, error)) return false;
    if (!profile) return true;
    std::string escapedAccount;
    std::string escapedUserName;
    std::string escapedDescription;
    if (!database_.escape(account, escapedAccount, error) ||
        !database_.escape(userName, escapedUserName, error) ||
        !database_.escape(description, escapedDescription, error)) {
        return false;
    }
    if (!database_.execute(
            "UPDATE users SET user_name = '" + escapedUserName +
                "', description = '" + escapedDescription +
                "' WHERE account = '" + escapedAccount + "'",
            error)) {
        return false;
    }
    return userProfile(account, profile, error);
}

bool MySqlUserRepository::updateAvatarPath(const std::string& account,
                                           const std::string& avatarPath,
                                           bool& updated,
                                           std::string& error) {
    updated = false;
    std::optional<bitevideo::UserProfile> profile;
    if (!userProfile(account, profile, error)) return false;
    if (!profile) return true;
    std::string escapedAccount;
    std::string escapedAvatarPath;
    if (!database_.escape(account, escapedAccount, error) ||
        !database_.escape(avatarPath, escapedAvatarPath, error)) {
        return false;
    }
    if (!database_.execute(
            "UPDATE users SET avatar_path = '" + escapedAvatarPath +
                "' WHERE account = '" + escapedAccount + "'",
            error)) {
        return false;
    }
    updated = true;
    return true;
}

bool MySqlUserRepository::passwordLogin(
    const std::string& account,
    const std::string& password,
    std::optional<bitevideo::UserProfile>& profile,
    std::string& error) {
    profile.reset();
    error.clear();
    if (account.empty() || password.empty()) return true;

    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) return false;
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(
            "SELECT account, password, user_name, description, avatar_path "
            "FROM users WHERE account = '" + escapedAccount + "' LIMIT 1",
            rows, error)) {
        return false;
    }
    if (rows.empty()) return true;
    if (rows.front().size() != 5) {
        error = "用户登录查询返回了不符合预期的字段数量";
        return false;
    }

    const std::string storedPassword = valueOrEmpty(rows.front()[1]);
    biteauth::PasswordVerification verification;
    if (!biteauth::verifyPassword(password, storedPassword, verification,
                                  error)) {
        return false;
    }
    if (!verification.matched) return true;

    if (verification.needsMigration) {
        std::string encoded;
        if (!biteauth::hashPassword(password, encoded, error)) return false;
        std::string escapedEncoded;
        std::string escapedStored;
        if (!database_.escape(encoded, escapedEncoded, error) ||
            !database_.escape(storedPassword, escapedStored, error)) {
            return false;
        }
        unsigned long long affected = 0;
        if (!database_.executeAffected(
                "UPDATE users SET password = '" + escapedEncoded +
                    "' WHERE account = '" + escapedAccount +
                    "' AND password = '" + escapedStored + "'",
                affected, error)) {
            return false;
        }
        if (affected == 0) {
            std::vector<bitedb::Database::QueryRow> currentRows;
            if (!database_.query(
                    "SELECT password FROM users WHERE account = '" +
                        escapedAccount + "' LIMIT 1",
                    currentRows, error)) {
                return false;
            }
            if (currentRows.empty() || currentRows.front().empty()) {
                profile.reset();
                return true;
            }
            if (!biteauth::verifyPassword(
                    password, valueOrEmpty(currentRows.front()[0]),
                    verification, error)) {
                return false;
            }
            if (!verification.matched) {
                profile.reset();
                return true;
            }
        }
    }

    bitevideo::UserProfile found;
    // The password column is deliberately skipped and never leaves the
    // repository boundary.
    found.account = valueOrEmpty(rows.front()[0]);
    found.userName = valueOrEmpty(rows.front()[2]);
    found.description = valueOrEmpty(rows.front()[3]);
    found.avatarPath = valueOrEmpty(rows.front()[4]);
    profile = std::move(found);
    return true;
}

bool MySqlUserRepository::createEmailCode(
    const std::string& email,
    bitevideo::EmailCodeSession& session,
    std::string& error) {
    session = {};
    error.clear();
    const std::string normalizedEmail = lowerCopy(email);
    static const std::regex EMAIL_RE(
        R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    if (!std::regex_match(normalizedEmail, EMAIL_RE)) {
        error = "邮箱格式错误";
        return true;
    }

    std::string code;
    std::string authcodeId;
    if (!biteauth::generateEmailCode(code, error) ||
        !biteauth::generateEmailCodeId(authcodeId, error)) {
        return false;
    }
    std::string escapedEmail;
    std::string escapedCode;
    std::string escapedAuthcodeId;
    if (!database_.escape(normalizedEmail, escapedEmail, error) ||
        !database_.escape(code, escapedCode, error) ||
        !database_.escape(authcodeId, escapedAuthcodeId, error)) {
        return false;
    }

    const std::string leaseSql =
        "INSERT INTO email_code_rate_limits "
        "(email, next_allowed_at, lease_id) VALUES ('" + escapedEmail +
        "', DATE_ADD(CURRENT_TIMESTAMP, INTERVAL " +
        std::to_string(biteauth::EMAIL_CODE_SEND_INTERVAL_SECONDS) +
        " SECOND), '" + escapedAuthcodeId + "') "
        "ON DUPLICATE KEY UPDATE "
        "lease_id = IF(next_allowed_at <= CURRENT_TIMESTAMP, "
        "VALUES(lease_id), lease_id), "
        "next_allowed_at = IF(next_allowed_at <= CURRENT_TIMESTAMP, "
        "VALUES(next_allowed_at), next_allowed_at)";
    if (!database_.execute(leaseSql, error)) return false;

    std::vector<bitedb::Database::QueryRow> leaseRows;
    if (!database_.query(
            "SELECT lease_id FROM email_code_rate_limits WHERE email = '" +
                escapedEmail + "' LIMIT 1",
            leaseRows, error)) {
        return false;
    }
    if (leaseRows.empty() || leaseRows.front().empty() ||
        valueOrEmpty(leaseRows.front()[0]) != authcodeId) {
        error = "验证码发送过于频繁，请稍后再试";
        return true;
    }

    const std::string insertSql =
        "INSERT INTO email_login_codes "
        "(authcode_id, email, authcode, consumed, expires_at, "
        "failed_attempts) VALUES ('" + escapedAuthcodeId + "', '" +
        escapedEmail + "', '" + escapedCode +
        "', 0, DATE_ADD(CURRENT_TIMESTAMP, INTERVAL " +
        std::to_string(biteauth::EMAIL_CODE_TTL_MINUTES) +
        " MINUTE), 0)";
    if (!database_.execute(insertSql, error)) return false;

    session.authcodeId = authcodeId;
    if (biteauth::emailDebugCodeEnabled()) session.debugCode = code;
    return true;
}

bool MySqlUserRepository::emailLogin(
    const std::string& email,
    const std::string& authcodeId,
    const std::string& authcode,
    std::optional<bitevideo::UserProfile>& profile,
    std::string& error) {
    profile.reset();
    error.clear();
    const std::string normalizedEmail = lowerCopy(email);
    std::string escapedEmail;
    std::string escapedAuthcodeId;
    std::string escapedAuthcode;
    if (!database_.escape(normalizedEmail, escapedEmail, error) ||
        !database_.escape(authcodeId, escapedAuthcodeId, error) ||
        !database_.escape(authcode, escapedAuthcode, error)) {
        return false;
    }

    unsigned long long consumed = 0;
    if (isSixDigitCode(authcode)) {
        const std::string consumeSql =
            "UPDATE email_login_codes SET consumed = 1, "
            "consumed_at = CURRENT_TIMESTAMP WHERE authcode_id = '" +
            escapedAuthcodeId + "' AND email = '" + escapedEmail +
            "' AND authcode = '" + escapedAuthcode +
            "' AND consumed = 0 AND expires_at > CURRENT_TIMESTAMP "
            "AND failed_attempts < " +
            std::to_string(biteauth::EMAIL_CODE_MAX_ATTEMPTS);
        if (!database_.executeAffected(consumeSql, consumed, error)) {
            return false;
        }
    }

    if (consumed != 1) {
        unsigned long long ignored = 0;
        const std::string failSql =
            "UPDATE email_login_codes SET failed_attempts = "
            "failed_attempts + 1 WHERE authcode_id = '" +
            escapedAuthcodeId + "' AND email = '" + escapedEmail +
            "' AND consumed = 0 AND expires_at > CURRENT_TIMESTAMP "
            "AND failed_attempts < " +
            std::to_string(biteauth::EMAIL_CODE_MAX_ATTEMPTS);
        if (!database_.executeAffected(failSql, ignored, error)) return false;
        return true;
    }

    std::optional<bitevideo::UserProfile> existing;
    if (!userProfile(normalizedEmail, existing, error)) return false;
    if (!existing) {
        std::string escapedUserName;
        if (!database_.escape(nameFromEmail(normalizedEmail), escapedUserName,
                              error)) {
            return false;
        }
        const std::string insertUserSql =
            "INSERT INTO users (account, password, user_name, description, "
            "avatar_path) VALUES ('" + escapedEmail + "', '', '" +
            escapedUserName + "', '', '') ON DUPLICATE KEY UPDATE "
            "account = VALUES(account)";
        if (!database_.execute(insertUserSql, error)) return false;
    }
    return userProfile(normalizedEmail, profile, error);
}

bool MySqlUserRepository::logout(const std::string& account,
                                 bool& knownUser,
                                 std::string& error) {
    std::optional<bitevideo::UserProfile> profile;
    if (!userProfile(account, profile, error)) return false;
    knownUser = profile.has_value();
    return true;
}

}  // namespace biteuser
