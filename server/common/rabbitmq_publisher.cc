#include "rabbitmq_publisher.h"

#ifdef VOD_ENABLE_REFERENCE_RUNTIME

#include "util.h"

#include <amqpcpp.h>
#include <amqpcpp/libevent.h>
#include <event2/event.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
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

std::string queueFor(const OutboxEvent& event) {
    if (event.exchange == "vod.transcode" && event.routingKey == "transcode.hls") {
        return "vod.transcode.hls";
    }
    if (event.exchange == "vod.file" && event.routingKey == "file.delete") {
        return "vod.file.delete";
    }
    if (event.exchange == "vod.cache") return "vod.cache.invalidate";
    if (event.exchange == "vod.search") return "vod.search.index";
    return event.exchange + "." + event.routingKey;
}

}  // namespace

RabbitMqPublisher::RabbitMqPublisher(biteconfig::RabbitMqSettings settings)
    : settings_(std::move(settings)) {}

bool RabbitMqPublisher::loadPassword(std::string& password,
                                     std::string& error) const {
    password = settings_.password;
    if (!password.empty()) return true;
    if (settings_.passwordFile.empty()) {
        error = "rabbitmq password or password_file is required";
        return false;
    }
    if (!biteutil::FUTIL::read(settings_.passwordFile, password)) {
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

bool RabbitMqPublisher::publish(const OutboxEvent& outbox,
                                std::string& error) {
    std::string password;
    if (!loadPassword(password, error)) return false;

    using EventBasePtr = std::unique_ptr<event_base, decltype(&event_base_free)>;
    EventBasePtr loop(event_base_new(), &event_base_free);
    if (!loop) {
        error = "cannot create RabbitMQ event loop";
        return false;
    }

    bool finished = false;
    bool confirmed = false;
    AMQP::LibEventHandler handler(loop.get());
    AMQP::TcpConnection connection(
        &handler,
        AMQP::Address(settings_.host,
                      static_cast<std::uint16_t>(settings_.port),
                      AMQP::Login(settings_.user, password),
                      settings_.virtualHost));
    AMQP::TcpChannel channel(&connection);
    AMQP::Reliable<> reliable(channel);

    const auto finish = [&](bool success, const std::string& message) {
        if (finished) return;
        finished = true;
        confirmed = success;
        if (!success) error = message;
        connection.close();
        event_base_loopbreak(loop.get());
    };

    channel.onError([&](const char* message) {
        finish(false, std::string("RabbitMQ channel error: ") + message);
    });

    const std::string queue = queueFor(outbox);
    const std::string deadQueue = queue + ".dead";
    AMQP::Table queueArguments;
    queueArguments["x-dead-letter-exchange"] = "vod.dlx";
    queueArguments["x-dead-letter-routing-key"] = deadQueue;

    channel.declareExchange("vod.dlx", AMQP::direct, AMQP::durable)
        .onSuccess([&] {
            channel.declareQueue(deadQueue, AMQP::durable)
                .onSuccess([&](const std::string&, std::uint32_t, std::uint32_t) {
                    channel.bindQueue("vod.dlx", deadQueue, deadQueue)
                        .onSuccess([&] {
                            channel.declareExchange(
                                outbox.exchange, AMQP::direct, AMQP::durable)
                                .onSuccess([&] {
                                    channel.declareQueue(
                                        queue, AMQP::durable, queueArguments)
                                        .onSuccess([&](const std::string&,
                                                       std::uint32_t,
                                                       std::uint32_t) {
                                            channel.bindQueue(
                                                outbox.exchange, queue,
                                                outbox.routingKey)
                                                .onSuccess([&] {
                                                    AMQP::Envelope envelope(
                                                        outbox.payload.data(),
                                                        outbox.payload.size());
                                                    envelope.setDeliveryMode(2);
                                                    envelope.setContentType(
                                                        "application/x-protobuf");
                                                    envelope.setMessageID(
                                                        outbox.eventId);
                                                    envelope.setCorrelationID(
                                                        outbox.requestId);
                                                    reliable.publish(
                                                        outbox.exchange,
                                                        outbox.routingKey,
                                                        envelope,
                                                        AMQP::mandatory)
                                                        .onAck([&] {
                                                            finish(true, "");
                                                        })
                                                        .onLost([&] {
                                                            finish(false,
                                                                "RabbitMQ publish was not confirmed");
                                                        });
                                                })
                                                .onError([&](const char* message) {
                                                    finish(false, message);
                                                });
                                        })
                                        .onError([&](const char* message) {
                                            finish(false, message);
                                        });
                                })
                                .onError([&](const char* message) {
                                    finish(false, message);
                                });
                        })
                        .onError([&](const char* message) {
                            finish(false, message);
                        });
                })
                .onError([&](const char* message) {
                    finish(false, message);
                });
        })
        .onError([&](const char* message) {
            finish(false, message);
        });

    timeval timeout{};
    timeout.tv_sec = 5;
    event_base_loopexit(loop.get(), &timeout);
    event_base_dispatch(loop.get());
    if (!finished) error = "RabbitMQ publisher confirmation timed out";
    return finished && confirmed;
}

}  // namespace biteevent

#endif
