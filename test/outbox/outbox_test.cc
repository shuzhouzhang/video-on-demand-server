#include "../../server/common/outbox.h"

#include <iostream>
#include <optional>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

class FakeRepository final : public biteevent::IOutboxRepository {
public:
    bool enqueue(const biteevent::OutboxEvent&, std::string&) override {
        return true;
    }

    bool claimNext(const std::string&, int,
                   std::optional<biteevent::OutboxEvent>& event,
                   std::string&) override {
        if (claimed || !next) {
            event.reset();
            return true;
        }
        claimed = true;
        event = next;
        return true;
    }

    bool markPublished(const biteevent::OutboxEvent&,
                       const std::string&, std::string&) override {
        published = true;
        return true;
    }

    bool markFailed(const biteevent::OutboxEvent&,
                    const std::string&,
                    const std::string& reason,
                    int,
                    int,
                    std::string&) override {
        failed = true;
        failureReason = reason;
        return true;
    }

    std::optional<biteevent::OutboxEvent> next;
    bool claimed = false;
    bool published = false;
    bool failed = false;
    std::string failureReason;
};

class FakePublisher final : public biteevent::IEventPublisher {
public:
    bool publish(const biteevent::OutboxEvent& event,
                 std::string& error) override {
        receivedEventId = event.eventId;
        if (succeed) return true;
        error = "broker unavailable";
        return false;
    }

    bool succeed = true;
    std::string receivedEventId;
};

}  // namespace

int main() {
    bool ok = true;
    biteevent::OutboxEvent event;
    event.id = 7;
    event.eventId = "event-7";

    FakeRepository successRepository;
    successRepository.next = event;
    FakePublisher successPublisher;
    biteevent::OutboxDispatcher successDispatcher(
        successRepository, successPublisher, 3, 5, 10);
    bool processed = false;
    std::string error;
    ok &= expect(successDispatcher.processOne("lease-a", processed, error),
                 "confirmed publish should succeed");
    ok &= expect(processed && successRepository.published &&
                     !successRepository.failed,
                 "confirmed publish should mark outbox row published");
    ok &= expect(successPublisher.receivedEventId == "event-7",
                 "publisher should receive claimed event");

    FakeRepository failedRepository;
    failedRepository.next = event;
    FakePublisher failedPublisher;
    failedPublisher.succeed = false;
    biteevent::OutboxDispatcher failedDispatcher(
        failedRepository, failedPublisher, 3, 5, 10);
    processed = false;
    error.clear();
    ok &= expect(!failedDispatcher.processOne("lease-b", processed, error),
                 "failed publish should be visible to worker");
    ok &= expect(processed && failedRepository.failed &&
                     !failedRepository.published,
                 "failed publish should return event to retry/dead flow");
    ok &= expect(error == "broker unavailable" &&
                     failedRepository.failureReason == error,
                 "broker failure reason should be preserved");

    FakeRepository emptyRepository;
    FakePublisher emptyPublisher;
    biteevent::OutboxDispatcher emptyDispatcher(
        emptyRepository, emptyPublisher, 3, 5, 10);
    processed = true;
    error.clear();
    ok &= expect(emptyDispatcher.processOne("lease-c", processed, error) &&
                     !processed,
                 "empty outbox should be a successful idle iteration");
    return ok ? 0 : 1;
}
