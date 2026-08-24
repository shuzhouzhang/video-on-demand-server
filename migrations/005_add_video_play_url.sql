SET @add_play_url_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE videos ADD COLUMN play_url VARCHAR(1024) NOT NULL DEFAULT ''D:/video-on-demand-client/test.mp4'' AFTER description',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'videos'
      AND COLUMN_NAME = 'play_url'
);

PREPARE add_play_url_stmt FROM @add_play_url_sql;

EXECUTE add_play_url_stmt;

DEALLOCATE PREPARE add_play_url_stmt;

UPDATE videos
SET play_url = 'D:/video-on-demand-client/test.mp4'
WHERE video_id IN ('video-001', 'video-002')
  AND (play_url IS NULL OR play_url = '');
