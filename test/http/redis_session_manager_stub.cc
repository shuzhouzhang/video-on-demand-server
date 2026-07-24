#include "../../server/common/redis_session_manager.h"

namespace bitesession {
RedisSessionManager::RedisSessionManager() = default;
RedisSessionManager::RedisSessionManager(const biteconfig::RedisSettings& settings)
    : settings_(settings) {}
RedisSessionManager::~RedisSessionManager() = default;
bool RedisSessionManager::connect(std::string& error) { error.clear(); return true; }
bool RedisSessionManager::enabled() const { return false; }
bool RedisSessionManager::createToken(const std::string&, std::string& token,
                                      std::string& error) {
    token.clear(); error.clear(); return true;
}
std::optional<std::string> RedisSessionManager::accountForToken(
    const std::string&, std::string& error) {
    error.clear(); return std::nullopt;
}
bool RedisSessionManager::deleteToken(const std::string&, std::string& error) {
    error.clear(); return true;
}
}  // namespace bitesession
