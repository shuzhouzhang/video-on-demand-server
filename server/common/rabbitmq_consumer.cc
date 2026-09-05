#include "rabbitmq_consumer.h"

#ifdef VOD_ENABLE_REFERENCE_RUNTIME

#include "util.h"

#include <amqpcpp.h>
#include <amqpcpp/libevent.h>
#include <event2/event.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>

namespace biteevent {
namespace {

std::string trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' ||
            value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : value.substr(first);
}

struct HandlerResult {
    bool success = false;
    std::string error;
};

struct SessionState {
    event_base* loop = nullptr;
    AMQP::TcpConnection* connection = nullptr;
    AMQP::TcpChannel* channel = nullptr;
    std::atomic<bool>* stopped = nullptr;
    RabbitMessageHandler* handler = nullptr;
    int maxAttempts = 3;
    bool failed = false;
    bool active = false;
    std::uint64_t deliveryTag = 0;
    std::string eventId;
    std::future<HandlerResult> future;
    std::unordered_map<std::string, int> attempts;
};

void pollSession(evutil_socket_t, short, void* argument) {
    auto& state = *static_cast<SessionState*>(argument);
    if (state.active &&
        state.future.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
        HandlerResult result = state.future.get();
        if (result.success) {
            state.channel->ack(state.deliveryTag);
            state.attempts.erase(state.eventId);
        } else {
            const int attempt = ++state.attempts[state.eventId];
            const bool retry = attempt < state.maxAttempts;
            state.channel->reject(state.deliveryTag,
                                  retry ? AMQP::requeue : 0);
            std::cerr << "RabbitMQ consumer handler failed for event_id="
                      << state.eventId << " attempt=" << attempt
                      << ": " << result.error << '\n';
            if (!retry) state.attempts.erase(state.eventId);
        }
        state.active = false;
        state.eventId.clear();
    }
    if ((state.stopped->load() || state.failed) && !state.active) {
        if (state.connection) state.connection->close();
        event_base_loopbreak(state.loop);
    }
}

}  // namespace

RabbitMqConsumer::RabbitMqConsumer(biteconfig::RabbitMqSettings settings,
                                   std::string exchange,
                                   std::string routingKey,
                                   std::string queue,
                                   RabbitMessageHandler handler)
    : settings_(std::move(settings)),
      exchange_(std::move(exchange)),
      routingKey_(std::move(routingKey)),
      queue_(std::move(queue)),
      handler_(std::move(handler)) {}

RabbitMqConsumer::~RabbitMqConsumer() { stop(); }

bool RabbitMqConsumer::loadPassword(std::string& password,
                                    std::string& error) const {
    password = settings_.password;
    if (!password.empty()) return true;
    if (settings_.passwordFile.empty() ||
        !biteutil::FUTIL::read(settings_.passwordFile, password)) {
        error = "cannot read rabbitmq password_file";
        return false;
    }
    password = trim(std::move(password));
    if (password.empty()) {
        error = "rabbitmq password_file is empty";
        return false;
    }
    return true;
}

void RabbitMqConsumer::start() {
    bool expected = true;
    if (!stopped_.compare_exchange_strong(expected, false)) return;
    thread_ = std::thread(&RabbitMqConsumer::run, this);
}

void RabbitMqConsumer::stop() {
    stopped_.store(true);
    if (thread_.joinable()) thread_.join();
}

void RabbitMqConsumer::run() {
    std::string password;
    std::string error;
    if (!loadPassword(password, error)) {
        std::cerr << "RabbitMQ consumer cannot start: " << error << '\n';
        return;
    }

    while (!stopped_.load()) {
        using EventBasePtr =
            std::unique_ptr<event_base, decltype(&event_base_free)>;
        EventBasePtr loop(event_base_new(), &event_base_free);
        if (!loop) {
            std::cerr << "RabbitMQ consumer cannot create event loop\n";
            return;
        }

        AMQP::LibEventHandler eventHandler(loop.get());
        AMQP::TcpConnection connection(
            &eventHandler,
            AMQP::Address(settings_.host,
                          static_cast<std::uint16_t>(settings_.port),
                          AMQP::Login(settings_.user, password),
                          settings_.virtualHost));
        AMQP::TcpChannel channel(&connection);
        SessionState state;
        state.loop = loop.get();
        state.connection = &connection;
        state.channel = &channel;
        state.stopped = &stopped_;
        state.handler = &handler_;
        state.maxAttempts = std::max(1, settings_.maxAttempts);

        channel.onError([&](const char* message) {
            std::cerr << "RabbitMQ consumer channel error: " << message
                      << '\n';
            state.failed = true;
        });

        AMQP::Table queueArguments;
        queueArguments["x-dead-letter-exchange"] = "vod.dlx";
        queueArguments["x-dead-letter-routing-key"] = queue_ + ".dead";
        channel.declareExchange("vod.dlx", AMQP::direct, AMQP::durable);
        channel.declareQueue(queue_ + ".dead", AMQP::durable);
        channel.bindQueue("vod.dlx", queue_ + ".dead", queue_ + ".dead");
        channel.declareExchange(exchange_, AMQP::direct, AMQP::durable);
        channel.declareQueue(queue_, AMQP::durable, queueArguments);
        channel.bindQueue(exchange_, queue_, routingKey_);
        channel.setQos(1);
        channel.consume(queue_).onReceived(
            [&](const AMQP::Message& message, std::uint64_t deliveryTag,
                bool redelivered) {
                if (state.active) {
                    channel.reject(deliveryTag, AMQP::requeue);
                    return;
                }
                ConsumedMessage consumed;
                consumed.eventId = message.messageID();
                consumed.body.assign(message.body(), message.bodySize());
                consumed.redelivered = redelivered;
                state.deliveryTag = deliveryTag;
                state.eventId = consumed.eventId;
                state.active = true;
                state.future = std::async(
                    std::launch::async,
                    [handler = state.handler, consumed = std::move(consumed)] {
                        HandlerResult result;
                        result.success = (*handler)(consumed, result.error);
                        return result;
                    });
            });

        timeval interval{};
        interval.tv_usec = 100000;
        using EventPtr = std::unique_ptr<event, decltype(&event_free)>;
        EventPtr poller(event_new(loop.get(), -1, EV_PERSIST, pollSession,
                                  &state),
                        &event_free);
        if (!poller || event_add(poller.get(), &interval) != 0) {
            std::cerr << "RabbitMQ consumer cannot create poll timer\n";
            return;
        }
        event_base_dispatch(loop.get());

        if (!stopped_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

}  // namespace biteevent

#endif
