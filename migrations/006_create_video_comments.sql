CREATE TABLE IF NOT EXISTS video_comments (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    video_id VARCHAR(64) NOT NULL,
    user_name VARCHAR(64) NOT NULL,
    account VARCHAR(128) NOT NULL,
    content VARCHAR(200) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_video_comments_video_time (video_id, created_at),
    KEY idx_video_comments_account (account),
    CONSTRAINT fk_video_comments_video
        FOREIGN KEY (video_id) REFERENCES videos(video_id)
        ON DELETE CASCADE
);
