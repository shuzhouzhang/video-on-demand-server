#include "svc_server.h"

#include <string>

int main(int argc, char* argv[]) {
    const std::string configPath =
        argc > 1 ? argv[1] : "conf/transcode_service.local.json";
    return svc_transcode::TranscodeServerBuilder()
        .withConfigPath(configPath)
        .start();
}
