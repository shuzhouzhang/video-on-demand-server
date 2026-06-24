CREATE TABLE IF NOT EXISTS videos (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    video_id VARCHAR(64) NOT NULL,
    title VARCHAR(255) NOT NULL,
    user_name VARCHAR(64) NOT NULL,
    published_on DATE NOT NULL,
    duration_seconds INT UNSIGNED NOT NULL,
    play_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
    like_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
    category VARCHAR(32) NOT NULL,
    tags JSON NOT NULL,
    description TEXT NOT NULL,
    status TINYINT NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_videos_video_id (video_id),
    KEY idx_videos_status_date (status, published_on)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO videos (
    video_id, title, user_name, published_on, duration_seconds,
    play_count, like_count, category, tags, description, status
) VALUES
    ('video-001', '真实后端返回的编程视频', 'BIT 用户', '2026-06-23',
     558, 36000, 256, '科技', JSON_ARRAY('编程开发', '软件工具'),
     '这条视频来自MySQL，用于验证客户端到真实后端的完整链路。', 1),
    ('video-002', '真实后端返回的美食视频', '接口测试员', '2026-06-22',
     728, 18000, 88, '美食', JSON_ARRAY('美食测评', '探店'),
     '这条视频用于验证列表、多标签和中文数据。', 1)
ON DUPLICATE KEY UPDATE
    title = VALUES(title),
    user_name = VALUES(user_name),
    published_on = VALUES(published_on),
    duration_seconds = VALUES(duration_seconds),
    play_count = VALUES(play_count),
    like_count = VALUES(like_count),
    category = VALUES(category),
    tags = VALUES(tags),
    description = VALUES(description),
    status = VALUES(status);
