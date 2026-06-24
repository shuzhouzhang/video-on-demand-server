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
    bool escape(const std::string& input,
                std::string& escaped,
                std::string& error);
    // 在同一事务中执行变更；只有首条语句真正改变数据时才执行后续语句。
    bool executeIfChanged(const std::string& changeSql,
                          const std::string& followupSql,
                          bool& changed,
                          std::string& error);

private:
    std::unique_ptr<odb::mysql::database> database_;
};

}  // namespace bitedb
