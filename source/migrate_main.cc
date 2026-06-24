#include "config.h"
#include "database.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(
        value.begin(), value.end(),
        [](unsigned char ch) { return std::isspace(ch); });
    const auto last = std::find_if_not(
        value.rbegin(), value.rend(),
        [](unsigned char ch) { return std::isspace(ch); }).base();
    return first < last ? std::string(first, last) : std::string();
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "用法: database_migrate <配置文件> <SQL迁移文件>\n";
        return 1;
    }

    std::string error;
    const auto settings = biteconfig::Config::load(argv[1], error);
    if (!settings) {
        std::cerr << error << '\n';
        return 1;
    }

    bitedb::Database database;
    if (!database.connect(settings->database, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::string sql;
    if (!biteutil::FUTIL::read(argv[2], sql)) {
        std::cerr << "无法读取迁移文件: " << argv[2] << '\n';
        return 1;
    }

    std::vector<std::string> statements;
    biteutil::STR::split(sql, ";", statements);
    std::size_t executed = 0;
    for (const std::string& rawStatement : statements) {
        const std::string statement = trim(rawStatement);
        if (statement.empty()) {
            continue;
        }
        if (!database.execute(statement, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        ++executed;
    }
    std::cout << "数据库迁移完成，共执行 " << executed << " 条语句\n";
    return 0;
}
