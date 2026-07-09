#pragma once

#include <string>

namespace svc_file {

class FileServerBuilder {
public:
    FileServerBuilder& withConfigPath(std::string configPath);
    int start() const;

private:
    std::string configPath_ = "conf/file_service.local.json";
};

}  // namespace svc_file
