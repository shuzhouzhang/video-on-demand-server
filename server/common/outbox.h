#pragma once

#include <optional>
#include <atomic>
#include <string>
#include <thread>

namespace bitedb {
class Database;
}

namespace biteevent {

struct OutboxEvent {
    unsigned long long id = 0;
    std::string eventId;
    std::string exchange;
    std::string routingKey;
    std::string eventType;
    std::string aggregateType;
    std::string aggregateId;
    std::string requestId;
    std::string payload;
    unsigned int attempts = 0;
};

class IOutboxRepository {
public:
    virtual ~IOutboxRepository() = default;
    virtual bool enqueue(const OutboxEvent& event, std::string& error) = 0;
    virtual bool claimNext(const std::string& leaseToken, int leaseSeconds,
                           std::optional<OutboxEvent>& event,
                           std::string& error) = 0;
    virtual bool markPublished(const OutboxEvent& event,
                               const std::string& leaseToken,
                               std::string& error) = 0;
    virtual bool markFailed(const OutboxEvent& event,
                            const std::string& leaseToken,
                            const std::string& reason,
                            int retryDelaySeconds,
                            int maxAttempts,
                            std::string& error) = 0;
};

class IEventPublisher {
public:
    virtual ~IEventPublisher() = default;
    // Success means the broker confirmed responsibility for this message.
    virtual bool publish(const OutboxEvent& event, std::string& error) = 0;
};

class MySqlOutboxRepository final : public IOutboxRepository {
public:
    explicit MySqlOutboxRepository(bitedb::Database& database);

    bool enqueue(const OutboxEvent& event, std::string& error) override;
    bool claimNext(const std::string& leaseToken, int leaseSeconds,
                   std::optional<OutboxEvent>& event,
                   std::string& error) override;
    bool markPublished(const OutboxEvent& event,
                       const std::string& leaseToken,
                       std::string& error) override;
    bool markFailed(const OutboxEvent& event,
                    const std::string& leaseToken,
                    const std::string& reason,
                    int retryDelaySeconds,
                    int maxAttempts,
                    std::string& error) override;

    bool buildInsertSql(const OutboxEvent& event, std::string& sql,
                        std::string& error);

private:
    bool readByLease(const std::string& leaseToken,
                     std::optional<OutboxEvent>& event,
                     std::string& error);

    bitedb::Database& database_;
};

class OutboxDispatcher {
public:
    OutboxDispatcher(IOutboxRepository& repository,
                     IEventPublisher& publisher,
                     int maxAttempts,
                     int retryDelaySeconds,
                     int leaseSeconds);

    bool processOne(const std::string& leaseToken, bool& processed,
                    std::string& error);

private:
    IOutboxRepository& repository_;
    IEventPublisher& publisher_;
    int maxAttempts_;
    int retryDelaySeconds_;
    int leaseSeconds_;
};

class OutboxWorker {
public:
    OutboxWorker(OutboxDispatcher& dispatcher, int pollIntervalMs);
    ~OutboxWorker();
    void start();
    void stop();

private:
    void run();

    OutboxDispatcher& dispatcher_;
    int pollIntervalMs_;
    std::atomic<bool> stopped_{true};
    std::thread thread_;
    unsigned long long counter_ = 0;
};

class ConsumedEventStore {
public:
    explicit ConsumedEventStore(bitedb::Database& database);
    bool markIfFirst(const std::string& consumerName,
                     const std::string& eventId,
                     bool& first,
                     std::string& error);

private:
    bitedb::Database& database_;
};

}  // namespace biteevent
