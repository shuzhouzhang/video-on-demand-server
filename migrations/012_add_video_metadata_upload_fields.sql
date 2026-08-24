SET @add_video_file_name_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE videos ADD COLUMN video_file_name VARCHAR(255) NOT NULL DEFAULT '''' AFTER play_url',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'videos'
      AND COLUMN_NAME = 'video_file_name'
);

PREPARE add_video_file_name_stmt FROM @add_video_file_name_sql;
EXECUTE add_video_file_name_stmt;
DEALLOCATE PREPARE add_video_file_name_stmt;

SET @add_cover_file_name_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE videos ADD COLUMN cover_file_name VARCHAR(255) NOT NULL DEFAULT '''' AFTER video_file_name',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'videos'
      AND COLUMN_NAME = 'cover_file_name'
);

PREPARE add_cover_file_name_stmt FROM @add_cover_file_name_sql;
EXECUTE add_cover_file_name_stmt;
DEALLOCATE PREPARE add_cover_file_name_stmt;
