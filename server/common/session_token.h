#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace bitesession {

using RandomBytesProvider =
    std::function<bool(unsigned char* output, std::size_t size)>;

// Generates a URL-safe login credential from 32 bytes supplied by
// OpenSSL RAND_bytes. There is deliberately no weak-random fallback.
bool generateSessionToken(std::string& token, std::string& error);

// Dependency-injected overload used to verify the hard failure path.
bool generateSessionToken(const RandomBytesProvider& randomBytes,
                          std::string& token,
                          std::string& error);

}  // namespace bitesession
