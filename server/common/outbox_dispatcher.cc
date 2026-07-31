#include "outbox.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include <unistd.h>

namespace biteevent {

OutboxDispatcher::OutboxDispatcher(IOutboxRepository& repository,
                                   IEventPublisher& publisher,
                                   int maxAttempts,
                                   int retryDelaySeconds,
                                   int leaseSeconds)
    : repository_(repository),
      publisher_(publisher),
      maxAttempts_(std::max(1, maxAttempts)),
      retryDelaySeconds_(std::max(1, retryDelaySeconds)),
      leaseSeconds_(std::max(1, leaseSeconds)) {}

bool OutboxDispatcher::processOne(const std::string& leaseToken,
                                  bool& processed,
                                  std::string& error) {
    processed = false;
    std::optional<OutboxEvent> event;
    if (!repository_.claimNext(leaseToken, leaseSeconds_, event, error)) {
        return false;
    }
    if (!event) return true;
    processed = true;
    std::string publishError;
    if (publisher_.publish(*event, publishError)) {
        return repository_.markPublished(*event, leaseToken, error);
    }
    if (!repository_.markFailed(*event, leaseToken, publishError,
                                retryDelaySeconds_, maxAttempts_, error)) {
        return false;
    }
    error = publishError;
    return false;
}

OutboxWorker::OutboxWorker(OutboxDispatcher& dispatcher, int pollIntervalMs)
    : dispatcher_(dispatcher),
      pollIntervalMs_(std::max(10, pollIntervalMs)) {}

OutboxWorker::~OutboxWorker() { stop(); }

void OutboxWorker::start() {
    bool expected = true;
    if (!stopped_.compare_exchange_strong(expected, false)) return;
    thread_ = std::thread(&OutboxWorker::run, this);
}

void OutboxWorker::stop() {
    stopped_.store(true);
    if (thread_.joinable()) thread_.join();
}

void OutboxWorker::run() {
    while (!stopped_.load()) {
        const std::string lease = "outbox-" + std::to_string(::getpid()) +
            "-" + std::to_string(++counter_);
        bool processed = false;
        std::string error;
        if (!dispatcher_.processOne(lease, processed, error) &&
            !error.empty()) {
            std::cerr << "outbox publish failed: " << error << '\n';
        }
        if (!processed) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(pollIntervalMs_));
        }
    }
}

}  // namespace biteevent
