CREATE TABLE IF NOT EXISTS stored_objects (
    object_id CHAR(36) NOT NULL,
    bundle_id CHAR(36) NOT NULL,
    owner_account VARCHAR(128) NOT NULL DEFAULT '',
    original_name VARCHAR(255) NOT NULL,
    storage_group VARCHAR(64) NOT NULL,
    remote_name VARCHAR(512) NOT NULL,
    mime_type VARCHAR(128) NOT NULL DEFAULT 'application/octet-stream',
    size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0,
    checksum_sha256 CHAR(64) NOT NULL DEFAULT '',
    object_role VARCHAR(32) NOT NULL DEFAULT 'MISC',
    status VARCHAR(16) NOT NULL DEFAULT 'ACTIVE',
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL,
    PRIMARY KEY (object_id),
    UNIQUE KEY uk_stored_objects_locator (storage_group, remote_name),
    KEY idx_stored_objects_bundle (bundle_id, object_role),
    KEY idx_stored_objects_owner (owner_account, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS outbox_events (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    event_id CHAR(36) NOT NULL,
    exchange_name VARCHAR(128) NOT NULL,
    routing_key VARCHAR(128) NOT NULL,
    event_type VARCHAR(64) NOT NULL,
    aggregate_type VARCHAR(64) NOT NULL,
    aggregate_id VARCHAR(128) NOT NULL,
    request_id VARCHAR(128) NOT NULL DEFAULT '',
    payload LONGBLOB NOT NULL,
    status VARCHAR(16) NOT NULL DEFAULT 'PENDING',
    attempts INT UNSIGNED NOT NULL DEFAULT 0,
    available_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    lease_token VARCHAR(128) NULL,
    lease_expires_at DATETIME NULL,
    last_error VARCHAR(1024) NOT NULL DEFAULT '',
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    published_at DATETIME NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uk_outbox_events_event_id (event_id),
    KEY idx_outbox_events_claim (status, available_at, id),
    KEY idx_outbox_events_aggregate (aggregate_type, aggregate_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS consumed_events (
    consumer_name VARCHAR(128) NOT NULL,
    event_id CHAR(36) NOT NULL,
    processed_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (consumer_name, event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
