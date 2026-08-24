SET @add_password_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE users ADD COLUMN password VARCHAR(128) NOT NULL DEFAULT ''123456'' AFTER account',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'users'
      AND COLUMN_NAME = 'password'
);

PREPARE add_password_stmt FROM @add_password_sql;

EXECUTE add_password_stmt;

DEALLOCATE PREPARE add_password_stmt;

CREATE TABLE IF NOT EXISTS email_login_codes (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    authcode_id VARCHAR(64) NOT NULL,
    email VARCHAR(128) NOT NULL,
    authcode VARCHAR(16) NOT NULL,
    consumed TINYINT NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_email_login_codes_authcode_id (authcode_id),
    KEY idx_email_login_codes_email (email)
);

UPDATE users
SET password = '123456'
WHERE account = 'bit-user-001'
  AND password = '';
