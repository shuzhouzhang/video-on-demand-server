#include "session_token.h"

#include <openssl/rand.h>

#include <array>
#include <climits>

namespace bitesession {
namespace {

constexpr std::size_t SESSION_TOKEN_BYTES = 32;
constexpr char HEX_DIGITS[] = "0123456789abcdef";

}  // namespace

bool generateSessionToken(const RandomBytesProvider& randomBytes,
                          std::string& token,
                          std::string& error) {
    token.clear();
    error.clear();
    std::array<unsigned char, SESSION_TOKEN_BYTES> bytes{};
    if (!randomBytes || !randomBytes(bytes.data(), bytes.size())) {
        error = "OpenSSL RAND_bytes failed; session token was not created";
        return false;
    }

    token.reserve(4 + bytes.size() * 2);
    token = "vod-";
    for (const unsigned char byte : bytes) {
        token.push_back(HEX_DIGITS[byte >> 4]);
        token.push_back(HEX_DIGITS[byte & 0x0f]);
    }
    return true;
}

bool generateSessionToken(std::string& token, std::string& error) {
    return generateSessionToken(
        [](unsigned char* output, std::size_t size) {
            return size <= static_cast<std::size_t>(INT_MAX) &&
                RAND_bytes(output, static_cast<int>(size)) == 1;
        },
        token, error);
}

}  // namespace bitesession
