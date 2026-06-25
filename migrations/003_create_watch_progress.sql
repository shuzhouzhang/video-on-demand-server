CREATE TABLE IF NOT EXISTS video_watch_progress (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    video_id VARCHAR(64) NOT NULL,
    account VARCHAR(128) NOT NULL,
    seconds INT UNSIGNED NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_video_watch_progress_video_account (video_id, account),
    KEY idx_video_watch_progress_account (account),
    CONSTRAINT fk_video_watch_progress_video
        FOREIGN KEY (video_id) REFERENCES videos(video_id)
        ON DELETE CASCADE
);
