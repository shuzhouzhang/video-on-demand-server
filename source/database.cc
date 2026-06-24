#include "database.h"

#include <odb/connection.hxx>
#include <odb/exception.hxx>
#include <odb/mysql/connection.hxx>
#include <odb/mysql/database.hxx>

#include <mysql/mysql.h>

#include <memory>

namespace bitedb {

Database::Database() = default;
Database::~Database() = default;

bool Database::connect(const biteconfig::DatabaseSettings& settings,
                       std::string& error) {
    error.clear();
    database_.reset();

    try {
        auto database = std::make_unique<odb::mysql::database>(
            settings.user, settings.password, settings.name, settings.host,
            settings.port);
        auto connection = database->connection();
        connection->execute("SET NAMES utf8mb4");
        connection->execute("SELECT 1");
        database_ = std::move(database);
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL 连接失败: " + std::string(exception.what());
        return false;
    }
}

bool Database::ping(std::string& error) {
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }

    try {
        auto connection = database_->connection();
        connection->execute("SELECT 1");
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL 健康检查失败: " + std::string(exception.what());
        return false;
    }
}

bool Database::isConnected() const {
    return database_ != nullptr;
}

bool Database::execute(const std::string& sql, std::string& error) {
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }

    try {
        auto connection = database_->connection();
        connection->execute(sql);
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL 执行失败: " + std::string(exception.what());
        return false;
    }
}

bool Database::query(const std::string& sql,
                     std::vector<QueryRow>& rows,
                     std::string& error) {
    rows.clear();
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }

    try {
        auto connection = database_->connection();
        MYSQL* handle = connection->handle();
        if (mysql_real_query(handle, sql.data(), sql.size()) != 0) {
            error = "MySQL 查询失败: " + std::string(mysql_error(handle));
            return false;
        }

        using ResultPtr = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;
        ResultPtr result(mysql_store_result(handle), &mysql_free_result);
        if (!result) {
            if (mysql_field_count(handle) == 0) {
                return true;
            }
            error = "MySQL 读取结果失败: " + std::string(mysql_error(handle));
            return false;
        }

        const unsigned int fieldCount = mysql_num_fields(result.get());
        while (MYSQL_ROW mysqlRow = mysql_fetch_row(result.get())) {
            const unsigned long* lengths = mysql_fetch_lengths(result.get());
            QueryRow row;
            row.reserve(fieldCount);
            for (unsigned int index = 0; index < fieldCount; ++index) {
                if (!mysqlRow[index]) {
                    row.push_back(std::nullopt);
                } else {
                    row.emplace_back(
                        std::string(mysqlRow[index], lengths[index]));
                }
            }
            rows.push_back(std::move(row));
        }
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL 查询失败: " + std::string(exception.what());
        return false;
    }
}

bool Database::escape(const std::string& input,
                      std::string& escaped,
                      std::string& error) {
    escaped.clear();
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }

    try {
        auto connection = database_->connection();
        escaped.resize(input.size() * 2 + 1);
        const unsigned long length = mysql_real_escape_string(
            connection->handle(), escaped.data(), input.data(), input.size());
        escaped.resize(length);
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL 参数转义失败: " + std::string(exception.what());
        return false;
    }
}

bool Database::executeIfChanged(const std::string& changeSql,
                                const std::string& followupSql,
                                bool& changed,
                                std::string& error) {
    changed = false;
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }

    try {
        auto connection = database_->connection();
        MYSQL* handle = connection->handle();
        if (mysql_autocommit(handle, false) != 0) {
            error = "MySQL 开启事务失败: " + std::string(mysql_error(handle));
            return false;
        }

        const auto fail = [&](const std::string& prefix) {
            error = prefix + std::string(mysql_error(handle));
            mysql_rollback(handle);
            mysql_autocommit(handle, true);
            return false;
        };

        if (mysql_real_query(handle, changeSql.data(), changeSql.size()) != 0) {
            return fail("MySQL 关系变更失败: ");
        }
        changed = mysql_affected_rows(handle) > 0;
        if (changed && mysql_real_query(
                handle, followupSql.data(), followupSql.size()) != 0) {
            return fail("MySQL 计数更新失败: ");
        }
        if (mysql_commit(handle) != 0) {
            return fail("MySQL 提交事务失败: ");
        }
        if (mysql_autocommit(handle, true) != 0) {
            error = "MySQL 恢复自动提交失败: " +
                std::string(mysql_error(handle));
            return false;
        }
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL 点赞事务失败: " + std::string(exception.what());
        return false;
    }
}

}  // namespace bitedb
