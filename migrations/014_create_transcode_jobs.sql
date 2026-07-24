SET @add_transcode_status_sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE videos ADD COLUMN transcode_status VARCHAR(16) NOT NULL DEFAULT ''READY'' AFTER review_status',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'videos'
      AND COLUMN_NAME = 'transcode_status'
);

PREPARE add_transcode_status_stmt FROM @add_transcode_status_sql;
EXECUTE add_transcode_status_stmt;
DEALLOCATE PREPARE add_transcode_status_stmt;

UPDATE videos
SET transcode_status = 'READY'
WHERE transcode_status IS NULL OR transcode_status = '';

CREATE TABLE IF NOT EXISTS transcode_jobs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    job_id VARCHAR(64) NOT NULL,
    video_id VARCHAR(64) NOT NULL,
    owner_account VARCHAR(128) NOT NULL,
    input_path VARCHAR(1024) NOT NULL,
    output_path VARCHAR(1024) NOT NULL,
    status VARCHAR(16) NOT NULL DEFAULT 'PENDING',
    attempts INT UNSIGNED NOT NULL DEFAULT 0,
    max_attempts INT UNSIGNED NOT NULL DEFAULT 3,
    lease_token VARCHAR(128) NOT NULL DEFAULT '',
    lease_until DATETIME NULL,
    next_attempt_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    error_message VARCHAR(512) NOT NULL DEFAULT '',
    started_at DATETIME NULL,
    finished_at DATETIME NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_transcode_jobs_job_id (job_id),
    UNIQUE KEY uk_transcode_jobs_video_id (video_id),
    KEY idx_transcode_jobs_claim (status, next_attempt_at, id),
    KEY idx_transcode_jobs_owner (owner_account, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
