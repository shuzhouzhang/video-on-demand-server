CREATE TABLE IF NOT EXISTS video_likes (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    video_id VARCHAR(64) NOT NULL,
    account VARCHAR(64) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_video_likes_video_account (video_id, account),
    KEY idx_video_likes_account (account),
    CONSTRAINT fk_video_likes_video
        FOREIGN KEY (video_id) REFERENCES videos(video_id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
