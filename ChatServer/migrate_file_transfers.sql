-- ============================================================
-- MinIO 文件传输记录表
-- 执行于数据库: wxchat（与 ChatServer config.ini [MySQL] Database 一致）
-- ============================================================
USE `wxchat`;

CREATE TABLE IF NOT EXISTS `file_transfers` (
    `id`           INT             NOT NULL AUTO_INCREMENT,
    `sender_uid`   INT             NOT NULL COMMENT '发送方 UID',
    `receiver_uid` INT             NOT NULL COMMENT '接收方 UID',
    `file_key`     VARCHAR(512)    NOT NULL COMMENT 'MinIO 对象路径',
    `filename`     VARCHAR(255)    NOT NULL COMMENT '原始文件名',
    `file_size`    BIGINT          NOT NULL DEFAULT 0 COMMENT '文件字节数',
    `download_url` TEXT            NOT NULL COMMENT '预签名 GET URL（7天有效）',
    `is_image`     TINYINT(1)      NOT NULL DEFAULT 0 COMMENT '1=图片消息',
    `delivered`    TINYINT(1)      NOT NULL DEFAULT 0 COMMENT '1=已推送给接收方',
    `created_at`   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_file_key` (`file_key`(255)),
    INDEX `idx_receiver_delivered` (`receiver_uid`, `delivered`),
    INDEX `idx_sender` (`sender_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='MinIO 对象存储文件传输记录';
