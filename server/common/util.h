/*
 * 通用工具：JSON 转换、文件读写、字符串切分和随机值生成。
 */
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <jsoncpp/json/json.h>

namespace biteutil {

class JSON {
public:
    // 将 Json::Value 转成字符串；styled=true 时保留便于阅读的缩进。
    static std::optional<std::string> serialize(const Json::Value& value,
                                                bool styled = false);

    // 将 JSON 字符串解析成 Json::Value；格式错误时返回 std::nullopt。
    static std::optional<Json::Value> unserialize(const std::string& input);
};

class FUTIL {
public:
    // 一次性读取或覆盖写入文件，适合配置和小型文本/二进制文件。
    static bool read(const std::string& filename, std::string& body);
    static bool write(const std::string& filename, const std::string& body);
};

class STR {
public:
    // 按 sep 切分字符串，跳过空片段，并把结果追加到 dst。
    static std::size_t split(const std::string& src,
                             const std::string& sep,
                             std::vector<std::string>& dst);
};

constexpr std::size_t UUID_SIZE = 16;

enum UuidType {
    MIX = 0,
    CHAR,
    DIGIT
};

class Random {
public:
    // 生成指定长度的随机字符串，可选择字母、数字或混合字符。
    static std::string code(std::size_t len = UUID_SIZE,
                            UuidType type = UuidType::MIX);

    // 生成闭区间 [min, max] 内的随机整数。
    static std::size_t number(std::size_t min, std::size_t max);
};

}  // namespace biteutil
