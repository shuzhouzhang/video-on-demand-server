CREATE TABLE IF NOT EXISTS video_barrages (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    video_id VARCHAR(64) NOT NULL,
    seconds INT UNSIGNED NOT NULL,
    text VARCHAR(30) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_video_barrages_video_seconds (video_id, seconds),
    CONSTRAINT fk_video_barrages_video
        FOREIGN KEY (video_id) REFERENCES videos(video_id)
        ON DELETE CASCADE
);
