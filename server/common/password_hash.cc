#include "password_hash.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace biteauth {
namespace {

constexpr std::string_view PREFIX = "$pbkdf2-sha256$";
constexpr int FORMAT_VERSION = 1;
constexpr int ITERATIONS = 210000;
constexpr std::size_t SALT_BYTES = 16;
constexpr std::size_t DIGEST_BYTES = 32;

std::string toHex(const unsigned char* bytes, std::size_t size) {
    static constexpr char HEX[] = "0123456789abcdef";
    std::string value(size * 2, '0');
    for (std::size_t index = 0; index < size; ++index) {
        value[index * 2] = HEX[(bytes[index] >> 4) & 0x0f];
        value[index * 2 + 1] = HEX[bytes[index] & 0x0f];
    }
    return value;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool fromHex(const std::string& value,
             std::vector<unsigned char>& bytes) {
    if (value.size() % 2 != 0) return false;
    bytes.clear();
    bytes.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const int high = hexValue(value[index]);
        const int low = hexValue(value[index + 1]);
        if (high < 0 || low < 0) return false;
        bytes.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

bool derive(const std::string& password,
            const unsigned char* salt,
            std::size_t saltSize,
            int iterations,
            std::array<unsigned char, DIGEST_BYTES>& digest,
            std::string& error) {
    if (password.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        saltSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "password input is too large";
        return false;
    }
    if (PKCS5_PBKDF2_HMAC(
            password.data(), static_cast<int>(password.size()), salt,
            static_cast<int>(saltSize), iterations, EVP_sha256(),
            static_cast<int>(digest.size()), digest.data()) != 1) {
        error = "PBKDF2-HMAC-SHA256 failed";
        return false;
    }
    return true;
}

bool constantTimeEqual(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    return left.empty() ||
        CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

bool parseInteger(std::string_view value, int& parsed) {
    if (value.empty()) return false;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} &&
        result.ptr == value.data() + value.size();
}

}  // namespace

bool isVersionedPasswordHash(const std::string& stored) {
    return stored.rfind(PREFIX.data(), 0) == 0;
}

bool hashPassword(const std::string& password,
                  std::string& encoded,
                  std::string& error) {
    encoded.clear();
    error.clear();
    if (password.empty()) {
        error = "password must not be empty";
        return false;
    }

    std::array<unsigned char, SALT_BYTES> salt{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        error = "secure random salt generation failed";
        return false;
    }
    std::array<unsigned char, DIGEST_BYTES> digest{};
    if (!derive(password, salt.data(), salt.size(), ITERATIONS,
                digest, error)) {
        return false;
    }

    encoded = std::string(PREFIX) + "v=" +
        std::to_string(FORMAT_VERSION) + "$i=" +
        std::to_string(ITERATIONS) + "$" +
        toHex(salt.data(), salt.size()) + "$" +
        toHex(digest.data(), digest.size());
    return true;
}

bool verifyPassword(const std::string& password,
                    const std::string& stored,
                    PasswordVerification& verification,
                    std::string& error) {
    verification = {};
    error.clear();
    if (password.empty() || stored.empty()) return true;

    if (!isVersionedPasswordHash(stored)) {
        verification.matched = constantTimeEqual(password, stored);
        verification.needsMigration = verification.matched;
        return true;
    }

    std::vector<std::string_view> parts;
    std::string_view remaining(stored);
    while (true) {
        const std::size_t separator = remaining.find('$');
        if (separator == std::string_view::npos) {
            parts.push_back(remaining);
            break;
        }
        parts.push_back(remaining.substr(0, separator));
        remaining.remove_prefix(separator + 1);
    }
    if (parts.size() != 6 || !parts[0].empty() ||
        parts[1] != "pbkdf2-sha256" ||
        parts[2].rfind("v=", 0) != 0 ||
        parts[3].rfind("i=", 0) != 0) {
        return true;
    }

    int version = 0;
    int iterations = 0;
    if (!parseInteger(parts[2].substr(2), version) ||
        !parseInteger(parts[3].substr(2), iterations) ||
        version != FORMAT_VERSION || iterations < 10000 ||
        iterations > 10000000) {
        return true;
    }

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!fromHex(std::string(parts[4]), salt) || salt.size() < 16 ||
        !fromHex(std::string(parts[5]), expected) ||
        expected.size() != DIGEST_BYTES) {
        return true;
    }

    std::array<unsigned char, DIGEST_BYTES> actual{};
    if (!derive(password, salt.data(), salt.size(), iterations,
                actual, error)) {
        return false;
    }
    verification.matched =
        CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
    verification.needsMigration =
        verification.matched && iterations != ITERATIONS;
    return true;
}

}  // namespace biteauth
