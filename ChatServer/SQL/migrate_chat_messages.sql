-- ============================================================
-- 聊天记录表
-- 执行于数据库: wxchat
-- ============================================================
USE `wxchat`;

CREATE TABLE IF NOT EXISTS `chat_messages` (
    `id`         BIGINT          NOT NULL AUTO_INCREMENT  COMMENT '自增主键',
    `from_uid`   INT             NOT NULL                 COMMENT '发送方 UID',
    `to_uid`     INT             NOT NULL                 COMMENT '接收方 UID',
    `content`    TEXT            NOT NULL                 COMMENT '消息内容',
    `created_at` DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '发送时间',
    PRIMARY KEY (`id`),
    -- 查询两人对话（正向 + 反向各一个索引，覆盖分页查询）
    INDEX `idx_conv_forward`  (`from_uid`, `to_uid`, `created_at`),
    INDEX `idx_conv_backward` (`to_uid`, `from_uid`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='点对点聊天记录';
