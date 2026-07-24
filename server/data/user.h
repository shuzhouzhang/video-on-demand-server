#pragma once

#include <string>

namespace bitevideo {

struct UserProfile {
    std::string account;
    std::string userName;
    std::string description;
    std::string avatarPath;
};

struct EmailCodeSession {
    std::string authcodeId;
    std::string debugCode;
};

struct AdminUser {
    std::string account;
    std::string userName;
    std::string role;
    std::string status;
    std::string createdAt;
};

struct UserAccess {
    std::string role;
    std::string status;
};

}  // namespace bitevideo
