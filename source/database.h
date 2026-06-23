/*
 * 数据库连接：根据配置建立 MySQL 连接，并提供启动健康检查。
 */
#pragma once

#include "config.h"

#include <memory>
#include <string>

namespace odb::mysql {
class database;
}

namespace bitedb {

class Database {
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 创建连接并执行一次轻量查询；失败时保持未连接状态并填写 error。
    bool connect(const biteconfig::DatabaseSettings& settings,
                 std::string& error);
    bool ping(std::string& error);
    bool isConnected() const;

private:
    std::unique_ptr<odb::mysql::database> database_;
};

}  // namespace bitedb
