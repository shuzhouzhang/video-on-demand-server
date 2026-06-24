/*
 * 数据库连接：根据配置建立 MySQL 连接，并提供启动健康检查。
 */
#pragma once

#include "config.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace odb::mysql {
class database;
}

namespace bitedb {

class Database {
public:
    using QueryRow = std::vector<std::optional<std::string>>;

    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 创建连接并执行一次轻量查询；失败时保持未连接状态并填写 error。
    bool connect(const biteconfig::DatabaseSettings& settings,
                 std::string& error);
    bool ping(std::string& error);
    bool isConnected() const;

    // 数据访问层使用这两个边界执行SQL；query把NULL保留为nullopt。
    bool execute(const std::string& sql, std::string& error);
    bool query(const std::string& sql,
               std::vector<QueryRow>& rows,
               std::string& error);

private:
    std::unique_ptr<odb::mysql::database> database_;
};

}  // namespace bitedb
