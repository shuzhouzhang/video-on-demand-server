ALTER TABLE users
MODIFY COLUMN password VARCHAR(255) NOT NULL DEFAULT '';

SET @add_email_expires_at_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE email_login_codes ADD COLUMN expires_at DATETIME NULL AFTER consumed',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'email_login_codes'
      AND COLUMN_NAME = 'expires_at'
);

PREPARE add_email_expires_at_stmt FROM @add_email_expires_at_sql;
EXECUTE add_email_expires_at_stmt;
DEALLOCATE PREPARE add_email_expires_at_stmt;

SET @add_email_failed_attempts_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE email_login_codes ADD COLUMN failed_attempts INT UNSIGNED NOT NULL DEFAULT 0 AFTER expires_at',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'email_login_codes'
      AND COLUMN_NAME = 'failed_attempts'
);

PREPARE add_email_failed_attempts_stmt FROM @add_email_failed_attempts_sql;
EXECUTE add_email_failed_attempts_stmt;
DEALLOCATE PREPARE add_email_failed_attempts_stmt;

SET @add_email_consumed_at_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE email_login_codes ADD COLUMN consumed_at DATETIME NULL AFTER failed_attempts',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'email_login_codes'
      AND COLUMN_NAME = 'consumed_at'
);

PREPARE add_email_consumed_at_stmt FROM @add_email_consumed_at_sql;
EXECUTE add_email_consumed_at_stmt;
DEALLOCATE PREPARE add_email_consumed_at_stmt;

UPDATE email_login_codes
SET expires_at = DATE_ADD(created_at, INTERVAL 10 MINUTE)
WHERE expires_at IS NULL;

ALTER TABLE email_login_codes
MODIFY COLUMN expires_at DATETIME NOT NULL;

SET @add_email_active_lookup_index_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE email_login_codes ADD INDEX idx_email_code_active (email, authcode_id, consumed, expires_at)',
        'SELECT 1'
    )
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'email_login_codes'
      AND INDEX_NAME = 'idx_email_code_active'
);

PREPARE add_email_active_lookup_index_stmt FROM @add_email_active_lookup_index_sql;
EXECUTE add_email_active_lookup_index_stmt;
DEALLOCATE PREPARE add_email_active_lookup_index_stmt;

CREATE TABLE IF NOT EXISTS email_code_rate_limits (
    email VARCHAR(128) NOT NULL,
    next_allowed_at DATETIME NOT NULL,
    lease_id VARCHAR(64) NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (email)
);
