#include "../../source/util.h"

#include <cstdio>
#include <iostream>
#include <set>
#include <vector>

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

    Json::Value source;
    source["name"] = "video-server";
    source["port"] = 9000;
    const auto json = biteutil::JSON::serialize(source);
    ok &= expect(json.has_value(), "JSON serialization");
    const auto parsed = biteutil::JSON::unserialize(json.value_or(""));
    ok &= expect(parsed && (*parsed)["port"].asInt() == 9000,
                 "JSON deserialization");
    ok &= expect(!biteutil::JSON::unserialize("{bad json").has_value(),
                 "invalid JSON is rejected");

    const std::string filename = "/tmp/biteutil_test.dat";
    const std::string content("abc\0xyz", 7);
    std::string loaded;
    ok &= expect(biteutil::FUTIL::write(filename, content), "file write");
    ok &= expect(biteutil::FUTIL::read(filename, loaded) && loaded == content,
                 "binary-safe file read");
    std::remove(filename.c_str());

    std::vector<std::string> parts;
    biteutil::STR::split("user,,video,file", ",", parts);
    ok &= expect(parts == std::vector<std::string>({"user", "video", "file"}),
                 "string split skips empty pieces");

    const std::string mixed = biteutil::Random::code();
    const std::string digits = biteutil::Random::code(6, biteutil::UuidType::DIGIT);
    const std::string letters = biteutil::Random::code(12, biteutil::UuidType::CHAR);
    ok &= expect(mixed.size() == biteutil::UUID_SIZE, "random ID length");
    ok &= expect(digits.size() == 6 &&
                     digits.find_first_not_of("0123456789") == std::string::npos,
                 "digit-only random code");
    ok &= expect(letters.size() == 12 &&
                     letters.find_first_not_of(
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") ==
                         std::string::npos,
                 "letter-only random code");

    bool inRange = true;
    for (int i = 0; i < 100; ++i) {
        const std::size_t value = biteutil::Random::number(50, 100);
        inRange &= value >= 50 && value <= 100;
    }
    ok &= expect(inRange, "random number range");

    return ok ? 0 : 1;
}
