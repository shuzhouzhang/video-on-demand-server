#pragma once

#include <string>

namespace biteauth {

struct PasswordVerification {
    bool matched = false;
    bool needsMigration = false;
};

// Encoded form:
// $pbkdf2-sha256$v=1$i=<iterations>$<salt-hex>$<digest-hex>
bool hashPassword(const std::string& password,
                  std::string& encoded,
                  std::string& error);

// Legacy plaintext values are accepted only for one-time migration. Values
// carrying the versioned prefix never fall back to plaintext comparison.
bool verifyPassword(const std::string& password,
                    const std::string& stored,
                    PasswordVerification& verification,
                    std::string& error);

bool isVersionedPasswordHash(const std::string& stored);

}  // namespace biteauth
