CREATE TABLE IF NOT EXISTS users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    account VARCHAR(128) NOT NULL,
    user_name VARCHAR(64) NOT NULL,
    description VARCHAR(100) NOT NULL DEFAULT '',
    avatar_path VARCHAR(1024) NOT NULL DEFAULT '',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_users_account (account)
);

INSERT INTO users (account, user_name, description, avatar_path)
VALUES
    ('bit-user-001', 'BIT 用户', '真实后端用户资料', '')
ON DUPLICATE KEY UPDATE
    user_name = VALUES(user_name);
