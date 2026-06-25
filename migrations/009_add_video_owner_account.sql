SET @add_owner_account_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE videos ADD COLUMN owner_account VARCHAR(128) NOT NULL DEFAULT '''' AFTER user_name',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'videos'
      AND COLUMN_NAME = 'owner_account'
);

PREPARE add_owner_account_stmt FROM @add_owner_account_sql;

EXECUTE add_owner_account_stmt;

DEALLOCATE PREPARE add_owner_account_stmt;

UPDATE videos
SET owner_account = 'bit-user-001'
WHERE video_id = 'video-001'
  AND owner_account = '';

UPDATE videos
SET owner_account = 'demo-food-user'
WHERE video_id = 'video-002'
  AND owner_account = '';
