#include "outbox.h"

#include "../database/database.h"

#include <algorithm>
#include <vector>

namespace biteevent {
namespace {

std::string text(const std::optional<std::string>& value) {
    return value.value_or("");
}

bool parseUnsigned(const std::optional<std::string>& value,
                   unsigned long long& parsed,
                   std::string& error) {
    try {
        parsed = std::stoull(text(value));
        return true;
    } catch (const std::exception&) {
        error = "outbox query returned an invalid integer";
        return false;
    }
}

bool parseEvent(const bitedb::Database::QueryRow& row,
                OutboxEvent& event,
                std::string& error) {
    if (row.size() != 10) {
        error = "outbox query returned unexpected fields";
        return false;
    }
    unsigned long long attempts = 0;
    if (!parseUnsigned(row[0], event.id, error) ||
        !parseUnsigned(row[9], attempts, error)) {
        return false;
    }
    event.eventId = text(row[1]);
    event.exchange = text(row[2]);
    event.routingKey = text(row[3]);
    event.eventType = text(row[4]);
    event.aggregateType = text(row[5]);
    event.aggregateId = text(row[6]);
    event.requestId = text(row[7]);
    event.payload = text(row[8]);
    event.attempts = static_cast<unsigned int>(attempts);
    return true;
}

constexpr const char* OUTBOX_SELECT =
    "SELECT CAST(id AS CHAR), event_id, exchange_name, routing_key, "
    "event_type, aggregate_type, aggregate_id, request_id, payload, "
    "CAST(attempts AS CHAR) FROM outbox_events WHERE ";

}  // namespace

MySqlOutboxRepository::MySqlOutboxRepository(bitedb::Database& database)
    : database_(database) {}

bool MySqlOutboxRepository::buildInsertSql(const OutboxEvent& event,
                                           std::string& sql,
                                           std::string& error) {
    std::string eventId;
    std::string exchange;
    std::string routingKey;
    std::string eventType;
    std::string aggregateType;
    std::string aggregateId;
    std::string requestId;
    std::string payload;
    if (!database_.escape(event.eventId, eventId, error) ||
        !database_.escape(event.exchange, exchange, error) ||
        !database_.escape(event.routingKey, routingKey, error) ||
        !database_.escape(event.eventType, eventType, error) ||
        !database_.escape(event.aggregateType, aggregateType, error) ||
        !database_.escape(event.aggregateId, aggregateId, error) ||
        !database_.escape(event.requestId, requestId, error) ||
        !database_.escape(event.payload, payload, error)) {
        return false;
    }
    if (eventId.empty() || exchange.empty() || eventType.empty()) {
        error = "outbox event_id, exchange and event_type are required";
        return false;
    }
    sql = "INSERT INTO outbox_events (event_id, exchange_name, routing_key, "
          "event_type, aggregate_type, aggregate_id, request_id, payload) "
          "VALUES ('" + eventId + "', '" + exchange + "', '" + routingKey +
          "', '" + eventType + "', '" + aggregateType + "', '" +
          aggregateId + "', '" + requestId + "', '" + payload + "')";
    return true;
}

bool MySqlOutboxRepository::enqueue(const OutboxEvent& event,
                                    std::string& error) {
    std::string sql;
    return buildInsertSql(event, sql, error) && database_.execute(sql, error);
}

bool MySqlOutboxRepository::readByLease(
    const std::string& leaseToken,
    std::optional<OutboxEvent>& event,
    std::string& error) {
    event.reset();
    std::string lease;
    if (!database_.escape(leaseToken, lease, error)) return false;
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(std::string(OUTBOX_SELECT) + "lease_token = '" +
                             lease + "' LIMIT 1",
                         rows, error)) {
        return false;
    }
    if (rows.empty()) return true;
    OutboxEvent value;
    if (!parseEvent(rows.front(), value, error)) return false;
    event = std::move(value);
    return true;
}

bool MySqlOutboxRepository::claimNext(
    const std::string& leaseToken,
    int leaseSeconds,
    std::optional<OutboxEvent>& event,
    std::string& error) {
    event.reset();
    std::string lease;
    if (!database_.escape(leaseToken, lease, error)) return false;
    unsigned long long affected = 0;
    const std::string sql =
        "UPDATE outbox_events SET status = 'PUBLISHING', lease_token = '" +
        lease + "', lease_expires_at = DATE_ADD(NOW(), INTERVAL " +
        std::to_string(std::max(1, leaseSeconds)) +
        " SECOND), attempts = attempts + 1 WHERE id = (SELECT id FROM "
        "(SELECT id FROM outbox_events WHERE (status = 'PENDING' OR "
        "(status = 'PUBLISHING' AND lease_expires_at < NOW())) AND "
        "available_at <= NOW() ORDER BY available_at, id LIMIT 1) candidate)";
    if (!database_.executeAffected(sql, affected, error)) return false;
    if (affected == 0) return true;
    return readByLease(leaseToken, event, error);
}

bool MySqlOutboxRepository::markPublished(const OutboxEvent& event,
                                          const std::string& leaseToken,
                                          std::string& error) {
    std::string lease;
    if (!database_.escape(leaseToken, lease, error)) return false;
    unsigned long long affected = 0;
    if (!database_.executeAffected(
            "UPDATE outbox_events SET status = 'PUBLISHED', published_at = "
            "NOW(), lease_token = NULL, lease_expires_at = NULL, "
            "last_error = '' WHERE id = " + std::to_string(event.id) +
            " AND status = 'PUBLISHING' AND lease_token = '" + lease + "'",
            affected, error)) {
        return false;
    }
    if (affected != 1) {
        error = "outbox publish completion rejected because lease was lost";
        return false;
    }
    return true;
}

bool MySqlOutboxRepository::markFailed(const OutboxEvent& event,
                                       const std::string& leaseToken,
                                       const std::string& reason,
                                       int retryDelaySeconds,
                                       int maxAttempts,
                                       std::string& error) {
    std::string lease;
    std::string escapedReason;
    if (!database_.escape(leaseToken, lease, error) ||
        !database_.escape(reason.substr(0, 1024), escapedReason, error)) {
        return false;
    }
    const bool exhausted = event.attempts >=
        static_cast<unsigned int>(std::max(1, maxAttempts));
    unsigned long long affected = 0;
    if (!database_.executeAffected(
            "UPDATE outbox_events SET status = '" +
            std::string(exhausted ? "DEAD" : "PENDING") +
            "', available_at = DATE_ADD(NOW(), INTERVAL " +
            std::to_string(std::max(1, retryDelaySeconds)) +
            " SECOND), lease_token = NULL, lease_expires_at = NULL, "
            "last_error = '" + escapedReason + "' WHERE id = " +
            std::to_string(event.id) +
            " AND status = 'PUBLISHING' AND lease_token = '" + lease + "'",
            affected, error)) {
        return false;
    }
    if (affected != 1) {
        error = "outbox failure update rejected because lease was lost";
        return false;
    }
    return true;
}

ConsumedEventStore::ConsumedEventStore(bitedb::Database& database)
    : database_(database) {}

bool ConsumedEventStore::wasProcessed(const std::string& consumerName,
                                      const std::string& eventId,
                                      bool& processed,
                                      std::string& error) {
    processed = false;
    std::string consumer;
    std::string event;
    if (!database_.escape(consumerName, consumer, error) ||
        !database_.escape(eventId, event, error)) {
        return false;
    }
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(
            "SELECT event_id FROM consumed_events WHERE consumer_name = '" +
                consumer + "' AND event_id = '" + event + "' LIMIT 1",
            rows, error)) {
        return false;
    }
    processed = !rows.empty();
    return true;
}

bool ConsumedEventStore::markIfFirst(const std::string& consumerName,
                                     const std::string& eventId,
                                     bool& first,
                                     std::string& error) {
    first = false;
    std::string consumer;
    std::string event;
    if (!database_.escape(consumerName, consumer, error) ||
        !database_.escape(eventId, event, error)) {
        return false;
    }
    unsigned long long affected = 0;
    if (!database_.executeAffected(
            "INSERT IGNORE INTO consumed_events (consumer_name, event_id) "
            "VALUES ('" + consumer + "', '" + event + "')",
            affected, error)) {
        return false;
    }
    first = affected == 1;
    return true;
}

}  // namespace biteevent
