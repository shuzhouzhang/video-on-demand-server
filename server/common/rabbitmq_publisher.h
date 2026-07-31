#pragma once

#include "config.h"
#include "outbox.h"

#ifdef VOD_ENABLE_REFERENCE_RUNTIME

namespace biteevent {

class RabbitMqPublisher final : public IEventPublisher {
public:
    explicit RabbitMqPublisher(biteconfig::RabbitMqSettings settings);
    bool publish(const OutboxEvent& event, std::string& error) override;

private:
    bool loadPassword(std::string& password, std::string& error) const;

    biteconfig::RabbitMqSettings settings_;
};

}  // namespace biteevent

#endif
