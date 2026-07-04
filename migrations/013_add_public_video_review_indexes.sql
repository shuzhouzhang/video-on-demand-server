SET @add_public_video_index_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'CREATE INDEX idx_videos_public_review ON videos (status, review_status, published_on, id)',
        'SELECT 1'
    )
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'videos'
      AND INDEX_NAME = 'idx_videos_public_review'
);

PREPARE add_public_video_index_stmt FROM @add_public_video_index_sql;
EXECUTE add_public_video_index_stmt;
DEALLOCATE PREPARE add_public_video_index_stmt;

SET @add_public_video_id_index_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'CREATE INDEX idx_videos_public_video_id ON videos (video_id, status, review_status)',
        'SELECT 1'
    )
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'videos'
      AND INDEX_NAME = 'idx_videos_public_video_id'
);

PREPARE add_public_video_id_index_stmt FROM @add_public_video_id_index_sql;
EXECUTE add_public_video_id_index_stmt;
DEALLOCATE PREPARE add_public_video_id_index_stmt;
