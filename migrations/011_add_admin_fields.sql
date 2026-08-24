SET @add_user_role_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE users ADD COLUMN role VARCHAR(32) NOT NULL DEFAULT ''普通用户'' AFTER user_name',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'users'
      AND COLUMN_NAME = 'role'
);

PREPARE add_user_role_stmt FROM @add_user_role_sql;
EXECUTE add_user_role_stmt;
DEALLOCATE PREPARE add_user_role_stmt;

SET @add_user_status_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE users ADD COLUMN status VARCHAR(16) NOT NULL DEFAULT ''启用'' AFTER role',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'users'
      AND COLUMN_NAME = 'status'
);

PREPARE add_user_status_stmt FROM @add_user_status_sql;
EXECUTE add_user_status_stmt;
DEALLOCATE PREPARE add_user_status_stmt;

INSERT INTO users (account, password, user_name, role, status, description, avatar_path)
VALUES
    ('admin@bit.com', '123456', '系统管理员', '超级管理员', '启用', '', ''),
    ('review@bit.com', '123456', '审核员', '管理员', '启用', '', '')
ON DUPLICATE KEY UPDATE
    user_name = VALUES(user_name),
    role = VALUES(role),
    status = VALUES(status);

UPDATE users
SET role = '普通用户', status = '启用'
WHERE account = 'bit-user-001';

SET @add_review_status_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE videos ADD COLUMN review_status VARCHAR(16) NOT NULL DEFAULT ''审核通过'' AFTER status',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'videos'
      AND COLUMN_NAME = 'review_status'
);

PREPARE add_review_status_stmt FROM @add_review_status_sql;
EXECUTE add_review_status_stmt;
DEALLOCATE PREPARE add_review_status_stmt;

UPDATE videos
SET review_status = '待审核'
WHERE video_id = 'video-001';

UPDATE videos
SET review_status = '审核通过'
WHERE video_id = 'video-002';
