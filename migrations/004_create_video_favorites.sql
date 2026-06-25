CREATE TABLE IF NOT EXISTS video_favorites (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    video_id VARCHAR(64) NOT NULL,
    account VARCHAR(128) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_video_favorites_video_account (video_id, account),
    KEY idx_video_favorites_account (account),
    CONSTRAINT fk_video_favorites_video
        FOREIGN KEY (video_id) REFERENCES videos(video_id)
        ON DELETE CASCADE
);
