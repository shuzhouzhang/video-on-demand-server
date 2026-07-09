#pragma once

#include <string>

namespace svc_user {

class UserServerBuilder {
public:
    UserServerBuilder& withConfigPath(std::string configPath);
    int start() const;

private:
    std::string configPath_ = "conf/user_service.local.json";
};

}  // namespace svc_user
