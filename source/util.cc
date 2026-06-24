#include "util.h"

#include "bitelog.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#include <sstream>

namespace biteutil {
namespace {

void logError(const std::string& message) {
    // 工具函数也可能在日志模块初始化前使用，因此先判断日志器是否存在。
    if (bitelog::g_logger) {
        ERR("{}", message);
    }
}

std::mt19937& randomEngine() {
    thread_local std::mt19937 engine(std::random_device{}());
    return engine;
}

}  // namespace

std::optional<std::string> JSON::serialize(const Json::Value& value,
                                           bool styled) {
    Json::StreamWriterBuilder builder;
    builder["emitUTF8"] = true;
    if (!styled) {
        builder["indentation"] = "";
    }

    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    std::ostringstream output;
    if (writer->write(value, &output) != 0) {
        logError("JSON 序列化失败");
        return std::nullopt;
    }
    return output.str();
}

std::optional<Json::Value> JSON::unserialize(const std::string& input) {
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value value;
    std::string errors;

    if (!reader->parse(input.data(), input.data() + input.size(), &value, &errors)) {
        logError("JSON 反序列化失败: " + errors);
        return std::nullopt;
    }
    return value;
}

bool FUTIL::read(const std::string& filename, std::string& body) {
    std::ifstream input(filename, std::ios::binary);
    if (!input.is_open()) {
        logError("打开文件失败: " + filename);
        return false;
    }

    body.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    if (input.bad()) {
        logError("读取文件失败: " + filename);
        return false;
    }
    return true;
}

bool FUTIL::write(const std::string& filename, const std::string& body) {
    std::ofstream output(filename, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        logError("打开文件失败: " + filename);
        return false;
    }

    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!output) {
        logError("写入文件失败: " + filename);
        return false;
    }
    return true;
}

std::size_t STR::split(const std::string& src,
                       const std::string& sep,
                       std::vector<std::string>& dst) {
    if (sep.empty()) {
        if (!src.empty()) {
            dst.push_back(src);
        }
        return dst.size();
    }

    std::size_t begin = 0;
    while (begin < src.size()) {
        const std::size_t end = src.find(sep, begin);
        const std::size_t count = (end == std::string::npos)
                                      ? std::string::npos
                                      : end - begin;
        const std::string part = src.substr(begin, count);
        if (!part.empty()) {
            dst.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + sep.size();
    }
    return dst.size();
}

std::string Random::code(std::size_t len, UuidType type) {
    static constexpr char DIGITS[] = "0123456789";
    static constexpr char LETTERS[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static constexpr char MIXED[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static std::atomic<unsigned int> sequence{0};

    const char* characters = MIXED;
    std::size_t characterCount = sizeof(MIXED) - 1;
    if (type == UuidType::DIGIT) {
        characters = DIGITS;
        characterCount = sizeof(DIGITS) - 1;
    } else if (type == UuidType::CHAR) {
        characters = LETTERS;
        characterCount = sizeof(LETTERS) - 1;
    }

    std::uniform_int_distribution<std::size_t> pick(0, characterCount - 1);
    std::string result(len, '0');
    for (char& ch : result) {
        ch = characters[pick(randomEngine())];
    }

    // 较长 ID 的末四位加入进程内递增编号，降低并发生成时的碰撞概率。
    if (len > 6 && type != UuidType::CHAR) {
        const unsigned int value = sequence.fetch_add(1) % 10000;
        std::ostringstream suffix;
        suffix << std::setw(4) << std::setfill('0') << value;
        result.replace(len - 4, 4, suffix.str());
    }
    return result;
}

std::size_t Random::number(std::size_t min, std::size_t max) {
    if (min > max) {
        std::swap(min, max);
    }
    std::uniform_int_distribution<std::size_t> distribution(min, max);
    return distribution(randomEngine());
}

}  // namespace biteutil
