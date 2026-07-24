#pragma once

#include <string>

namespace biteauth {

inline constexpr int EMAIL_CODE_TTL_MINUTES = 10;
inline constexpr int EMAIL_CODE_MAX_ATTEMPTS = 5;
inline constexpr int EMAIL_CODE_SEND_INTERVAL_SECONDS = 60;

bool generateEmailCode(std::string& code, std::string& error);
bool generateEmailCodeId(std::string& id, std::string& error);
bool emailDebugCodeEnabled();

}  // namespace biteauth
