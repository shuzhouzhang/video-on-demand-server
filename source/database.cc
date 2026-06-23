#include "database.h"

#include <odb/connection.hxx>
#include <odb/exception.hxx>
#include <odb/mysql/database.hxx>

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

}  // namespace bitedb
