#include "../../source/database.h"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    std::cout << "[PASS] " << message << '\n';
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    std::string error;
    bitedb::Database database;

    ok &= expect(!database.isConnected(), "database starts disconnected");
    ok &= expect(!database.ping(error) && !error.empty(),
                 "ping reports disconnected state");

    // 端口 1 不提供 MySQL 服务，用于稳定验证连接失败处理。
    const biteconfig::DatabaseSettings unavailable{
        "127.0.0.1", 1, "video_app", "", "video_on_demand"};
    ok &= expect(!database.connect(unavailable, error) && !error.empty(),
                 "connection failure returns an error");
    ok &= expect(!database.isConnected(),
                 "failed connection does not leave a usable handle");

    return ok ? 0 : 1;
}
