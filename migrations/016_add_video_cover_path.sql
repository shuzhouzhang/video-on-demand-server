SET @add_cover_path_sql = (
 SELECT IF(COUNT(*) = 0,
  'ALTER TABLE videos ADD COLUMN cover_path VARCHAR(1024) NOT NULL DEFAULT ''''',
  'SELECT 1')
 FROM information_schema.COLUMNS
 WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'videos' AND COLUMN_NAME = 'cover_path'
);
PREPARE add_cover_path_stmt FROM @add_cover_path_sql;
EXECUTE add_cover_path_stmt;
DEALLOCATE PREPARE add_cover_path_stmt;
