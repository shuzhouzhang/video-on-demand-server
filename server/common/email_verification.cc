#include "email_verification.h"

#include <openssl/rand.h>

#include <array>
#include <cstdlib>

namespace biteauth {
namespace {

std::string toHex(const unsigned char* bytes, std::size_t size) {
    static constexpr char HEX[] = "0123456789abcdef";
    std::string value(size * 2, '0');
    for (std::size_t index = 0; index < size; ++index) {
        value[index * 2] = HEX[(bytes[index] >> 4) & 0x0f];
        value[index * 2 + 1] = HEX[bytes[index] & 0x0f];
    }
    return value;
}

}  // namespace

bool generateEmailCode(std::string& code, std::string& error) {
    code.clear();
    error.clear();
    // Rejection sampling avoids modulo bias: 250 is divisible by 10.
    while (code.size() < 6) {
        std::array<unsigned char, 16> random{};
        if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1) {
            error = "secure verification code generation failed";
            return false;
        }
        for (const unsigned char value : random) {
            if (value < 250) {
                code.push_back(static_cast<char>('0' + value % 10));
                if (code.size() == 6) break;
            }
        }
    }
    return true;
}

bool generateEmailCodeId(std::string& id, std::string& error) {
    std::array<unsigned char, 16> random{};
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1) {
        error = "secure verification id generation failed";
        return false;
    }
    id = "email-code-" + toHex(random.data(), random.size());
    error.clear();
    return true;
}

bool emailDebugCodeEnabled() {
    const char* value = std::getenv("VIDEO_ENABLE_EMAIL_DEBUG_CODE");
    return value && std::string(value) == "1";
}

}  // namespace biteauth
