#pragma once

#include "config.h"

#ifdef VOD_ENABLE_REFERENCE_RUNTIME

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace biteevent {

struct ConsumedMessage {
    std::string eventId;
    std::string body;
    bool redelivered = false;
};

using RabbitMessageHandler =
    std::function<bool(const ConsumedMessage&, std::string& error)>;

class RabbitMqConsumer {
public:
    RabbitMqConsumer(biteconfig::RabbitMqSettings settings,
                     std::string exchange,
                     std::string routingKey,
                     std::string queue,
                     RabbitMessageHandler handler);
    ~RabbitMqConsumer();

    RabbitMqConsumer(const RabbitMqConsumer&) = delete;
    RabbitMqConsumer& operator=(const RabbitMqConsumer&) = delete;

    void start();
    void stop();

private:
    void run();
    bool loadPassword(std::string& password, std::string& error) const;
    std::string address(const std::string& password) const;

    biteconfig::RabbitMqSettings settings_;
    std::string exchange_;
    std::string routingKey_;
    std::string queue_;
    RabbitMessageHandler handler_;
    std::atomic<bool> stopped_{true};
    std::thread thread_;
};

}  // namespace biteevent

#endif
