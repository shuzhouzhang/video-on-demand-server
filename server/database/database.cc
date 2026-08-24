#include "database.h"

#include <mysql/plugin_auth_common.h>

#include <odb/connection.hxx>
#include <odb/exception.hxx>
#include <odb/mysql/connection.hxx>
#include <odb/mysql/connection-factory.hxx>
#include <odb/mysql/database.hxx>

#include <mysql/mysql.h>

#include <algorithm>
#include <memory>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

namespace {

struct MysqlStatementDeleter {
    void operator()(MYSQL_STMT* statement) const noexcept {
        if (statement) mysql_stmt_close(statement);
    }
};

using MysqlStatementPtr =
    std::unique_ptr<MYSQL_STMT, MysqlStatementDeleter>;
using MysqlBool =
    std::remove_pointer_t<decltype(MYSQL_BIND{}.is_null)>;

bool prepareAndExecute(MYSQL* handle,
                       const std::string& sql,
                       const std::vector<std::string>& parameters,
                       MysqlStatementPtr& statement,
                       std::string& error) {
    statement.reset(mysql_stmt_init(handle));
    if (!statement) {
        error = "MySQL prepared statement allocation failed";
        return false;
    }
    if (mysql_stmt_prepare(statement.get(), sql.data(), sql.size()) != 0) {
        error = "MySQL prepared statement failed: " +
            std::string(mysql_stmt_error(statement.get()));
        return false;
    }
    if (mysql_stmt_param_count(statement.get()) != parameters.size()) {
        error = "MySQL prepared statement parameter count mismatch";
        return false;
    }

    std::vector<MYSQL_BIND> bindings(parameters.size());
    std::vector<unsigned long> lengths(parameters.size());
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        std::memset(&bindings[index], 0, sizeof(MYSQL_BIND));
        lengths[index] = static_cast<unsigned long>(parameters[index].size());
        bindings[index].buffer_type = MYSQL_TYPE_STRING;
        bindings[index].buffer =
            const_cast<char*>(parameters[index].data());
        bindings[index].buffer_length = lengths[index];
        bindings[index].length = &lengths[index];
    }
    if (!bindings.empty() &&
        mysql_stmt_bind_param(statement.get(), bindings.data()) != 0) {
        error = "MySQL prepared parameter binding failed: " +
            std::string(mysql_stmt_error(statement.get()));
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        error = "MySQL prepared execution failed: " +
            std::string(mysql_stmt_error(statement.get()));
        return false;
    }
    return true;
}

bool preparedAffected(MYSQL* handle,
                      const std::string& sql,
                      const std::vector<std::string>& parameters,
                      unsigned long long& affectedRows,
                      std::string& error) {
    affectedRows = 0;
    MysqlStatementPtr statement;
    if (!prepareAndExecute(handle, sql, parameters, statement, error)) {
        return false;
    }
    const my_ulonglong affected = mysql_stmt_affected_rows(statement.get());
    if (affected == static_cast<my_ulonglong>(-1)) {
        error = "MySQL prepared affected-row lookup failed: " +
            std::string(mysql_stmt_error(statement.get()));
        return false;
    }
    affectedRows = static_cast<unsigned long long>(affected);
    return true;
}

}  // namespace

namespace bitedb {

Database::Database() = default;
Database::~Database() = default;

bool Database::connect(const biteconfig::DatabaseSettings& settings,
                       std::string& error) {
    std::unique_lock<std::shared_mutex> lock(databaseMutex_);
    error.clear();
    database_.reset();

    try {
        constexpr std::size_t MAX_DATABASE_CONNECTIONS = 32;
        auto factory = std::make_unique<odb::mysql::connection_pool_factory>(
            MAX_DATABASE_CONNECTIONS, 1, true);
        auto database = std::make_unique<odb::mysql::database>(
            settings.user, settings.password, settings.name, settings.host,
            settings.port, nullptr, "utf8mb4", 0, std::move(factory));
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
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
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
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
    return database_ != nullptr;
}

bool Database::execute(const std::string& sql, std::string& error) {
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
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
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
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

bool Database::executePrepared(
    const std::string& sql,
    const std::vector<std::string>& parameters,
    std::string& error) {
    unsigned long long ignored = 0;
    return executeAffectedPrepared(sql, parameters, ignored, error);
}

bool Database::executeAffectedPrepared(
    const std::string& sql,
    const std::vector<std::string>& parameters,
    unsigned long long& affectedRows,
    std::string& error) {
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
    affectedRows = 0;
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }
    try {
        auto connection = database_->connection();
        return preparedAffected(connection->handle(), sql, parameters,
                                affectedRows, error);
    } catch (const odb::exception& exception) {
        error = "MySQL prepared execution failed: " +
            std::string(exception.what());
        return false;
    }
}

bool Database::queryPrepared(
    const std::string& sql,
    const std::vector<std::string>& parameters,
    std::vector<QueryRow>& rows,
    std::string& error) {
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
    rows.clear();
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }

    try {
        auto connection = database_->connection();
        MysqlStatementPtr statement;
        if (!prepareAndExecute(connection->handle(), sql, parameters,
                               statement, error)) {
            return false;
        }
        MysqlBool updateMaxLength = 1;
        if (mysql_stmt_attr_set(statement.get(), STMT_ATTR_UPDATE_MAX_LENGTH,
                                &updateMaxLength) != 0 ||
            mysql_stmt_store_result(statement.get()) != 0) {
            error = "MySQL prepared result buffering failed: " +
                std::string(mysql_stmt_error(statement.get()));
            return false;
        }

        using MetadataPtr =
            std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;
        MetadataPtr metadata(mysql_stmt_result_metadata(statement.get()),
                             &mysql_free_result);
        if (!metadata) {
            if (mysql_stmt_field_count(statement.get()) == 0) return true;
            error = "MySQL prepared result metadata failed: " +
                std::string(mysql_stmt_error(statement.get()));
            return false;
        }

        const unsigned int fieldCount = mysql_num_fields(metadata.get());
        MYSQL_FIELD* fields = mysql_fetch_fields(metadata.get());
        std::vector<MYSQL_BIND> bindings(fieldCount);
        std::vector<std::vector<char>> buffers(fieldCount);
        std::vector<unsigned long> lengths(fieldCount);
        auto isNull = std::make_unique<MysqlBool[]>(fieldCount);
        auto truncated = std::make_unique<MysqlBool[]>(fieldCount);
        for (unsigned int index = 0; index < fieldCount; ++index) {
            std::memset(&bindings[index], 0, sizeof(MYSQL_BIND));
            const std::size_t size =
                std::max<std::size_t>(1, fields[index].max_length + 1);
            buffers[index].resize(size);
            isNull[index] = 0;
            truncated[index] = 0;
            bindings[index].buffer_type = MYSQL_TYPE_STRING;
            bindings[index].buffer = buffers[index].data();
            bindings[index].buffer_length =
                static_cast<unsigned long>(buffers[index].size());
            bindings[index].length = &lengths[index];
            bindings[index].is_null = &isNull[index];
            bindings[index].error = &truncated[index];
        }
        if (fieldCount > 0 &&
            mysql_stmt_bind_result(statement.get(), bindings.data()) != 0) {
            error = "MySQL prepared result binding failed: " +
                std::string(mysql_stmt_error(statement.get()));
            return false;
        }

        while (true) {
            const int result = mysql_stmt_fetch(statement.get());
            if (result == MYSQL_NO_DATA) break;
            if (result != 0 ||
                std::any_of(truncated.get(), truncated.get() + fieldCount,
                            [](MysqlBool value) { return value != 0; })) {
                error = "MySQL prepared result fetch failed: " +
                    std::string(mysql_stmt_error(statement.get()));
                return false;
            }
            QueryRow row;
            row.reserve(fieldCount);
            for (unsigned int index = 0; index < fieldCount; ++index) {
                if (isNull[index]) {
                    row.push_back(std::nullopt);
                } else {
                    row.emplace_back(std::string(buffers[index].data(),
                                                 lengths[index]));
                }
            }
            rows.push_back(std::move(row));
        }
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL prepared query failed: " +
            std::string(exception.what());
        return false;
    }
}

bool Database::escape(const std::string& input,
                      std::string& escaped,
                      std::string& error) {
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
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

bool Database::executeAffected(const std::string& sql,
                               unsigned long long& affectedRows,
                               std::string& error) {
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
    affectedRows = 0;
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }

    try {
        auto connection = database_->connection();
        MYSQL* handle = connection->handle();
        if (mysql_real_query(handle, sql.data(), sql.size()) != 0) {
            error = "MySQL 执行失败: " + std::string(mysql_error(handle));
            return false;
        }
        const my_ulonglong affected = mysql_affected_rows(handle);
        if (affected == static_cast<my_ulonglong>(-1)) {
            error = "MySQL 无法读取受影响行数: " +
                std::string(mysql_error(handle));
            return false;
        }
        affectedRows = static_cast<unsigned long long>(affected);
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL 执行失败: " + std::string(exception.what());
        return false;
    }
}

bool Database::executeTransaction(const std::vector<std::string>& statements,
                                  std::string& error) {
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
    error.clear();
    if (!database_) {
        error = "MySQL 尚未连接";
        return false;
    }
    if (statements.empty()) return true;

    try {
        auto connection = database_->connection();
        MYSQL* handle = connection->handle();
        if (mysql_autocommit(handle, false) != 0) {
            error = "MySQL 开启事务失败: " + std::string(mysql_error(handle));
            return false;
        }
        for (const auto& statement : statements) {
            if (mysql_real_query(handle, statement.data(), statement.size()) != 0) {
                error = "MySQL 事务执行失败: " +
                    std::string(mysql_error(handle));
                mysql_rollback(handle);
                mysql_autocommit(handle, true);
                return false;
            }
        }
        if (mysql_commit(handle) != 0) {
            error = "MySQL 提交事务失败: " +
                std::string(mysql_error(handle));
            mysql_rollback(handle);
            mysql_autocommit(handle, true);
            return false;
        }
        if (mysql_autocommit(handle, true) != 0) {
            error = "MySQL 恢复自动提交失败: " +
                std::string(mysql_error(handle));
            return false;
        }
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL 事务执行失败: " + std::string(exception.what());
        return false;
    }
}

bool Database::executeIfChanged(const std::string& changeSql,
                                const std::string& followupSql,
                                bool& changed,
                                std::string& error) {
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
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

bool Database::executeIfChangedPrepared(
    const std::string& changeSql,
    const std::vector<std::string>& changeParameters,
    const std::string& followupSql,
    const std::vector<std::string>& followupParameters,
    bool& changed,
    std::string& error) {
    std::shared_lock<std::shared_mutex> lock(databaseMutex_);
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
        const auto fail = [&]() {
            mysql_rollback(handle);
            mysql_autocommit(handle, true);
            return false;
        };
        unsigned long long affected = 0;
        if (!preparedAffected(handle, changeSql, changeParameters, affected,
                              error)) {
            return fail();
        }
        changed = affected > 0;
        if (changed) {
            unsigned long long ignored = 0;
            if (!preparedAffected(handle, followupSql, followupParameters,
                                  ignored, error)) {
                return fail();
            }
        }
        if (mysql_commit(handle) != 0) {
            error = "MySQL 提交事务失败: " + std::string(mysql_error(handle));
            return fail();
        }
        if (mysql_autocommit(handle, true) != 0) {
            error = "MySQL 恢复自动提交失败: " +
                std::string(mysql_error(handle));
            return false;
        }
        return true;
    } catch (const odb::exception& exception) {
        error = "MySQL prepared transaction failed: " +
            std::string(exception.what());
        return false;
    }
}

}  // namespace bitedb
