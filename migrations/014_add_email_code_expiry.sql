SET @add_email_code_expiry_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE email_login_codes ADD COLUMN expires_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP AFTER consumed',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'email_login_codes'
      AND COLUMN_NAME = 'expires_at'
);

PREPARE add_email_code_expiry_stmt FROM @add_email_code_expiry_sql;
EXECUTE add_email_code_expiry_stmt;
DEALLOCATE PREPARE add_email_code_expiry_stmt;

UPDATE email_login_codes
SET expires_at = DATE_ADD(created_at, INTERVAL 10 MINUTE)
WHERE expires_at = created_at;

SET @add_email_code_lookup_index_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'CREATE INDEX idx_email_login_codes_lookup ON email_login_codes (authcode_id, email, authcode, consumed, expires_at)',
        'SELECT 1'
    )
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'email_login_codes'
      AND INDEX_NAME = 'idx_email_login_codes_lookup'
);

PREPARE add_email_code_lookup_index_stmt FROM @add_email_code_lookup_index_sql;
EXECUTE add_email_code_lookup_index_stmt;
DEALLOCATE PREPARE add_email_code_lookup_index_stmt;
