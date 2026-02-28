-- ──────────────────────────────────────────────────────────────────────────
-- 数据库初始化脚本
-- 运行方式: mysql -u root -p < init_database.sql
-- ──────────────────────────────────────────────────────────────────────────

CREATE DATABASE IF NOT EXISTS `weixinchat` DEFAULT CHARSET utf8mb4;
USE `weixinchat`;

-- ── 用户表 ──────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS `chat_user` (
    `uid`         INT          NOT NULL AUTO_INCREMENT,
    `name`        VARCHAR(64)  NOT NULL UNIQUE,
    `email`       VARCHAR(128) NOT NULL UNIQUE,
    `pwd`         VARCHAR(256) NOT NULL,
    `signature`   VARCHAR(256) DEFAULT '',
    `avatar_path` VARCHAR(512) DEFAULT '',
    `created_at`  DATETIME     DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- UID 递增辅助表（配合 RegUser 使用）
CREATE TABLE IF NOT EXISTS `user_id` (
    `id` INT NOT NULL DEFAULT 0
) ENGINE=InnoDB;
INSERT IGNORE INTO `user_id` (`id`) SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM `user_id`);

-- ── 好友关系表 ─────────────────────────────────────────────────────────────
-- 双向存储：uid_A→uid_B 和 uid_B→uid_A 各一条，便于按 user_uid 快速查询好友列表
CREATE TABLE IF NOT EXISTS `chat_friends` (
    `id`         INT      NOT NULL AUTO_INCREMENT,
    `user_uid`   INT      NOT NULL,
    `friend_uid` INT      NOT NULL,
    `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_friendship` (`user_uid`, `friend_uid`),
    INDEX `idx_user_uid` (`user_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── 好友申请表（离线申请持久化）─────────────────────────────────────────────
-- status: 0=待处理 1=已接受 2=已拒绝
CREATE TABLE IF NOT EXISTS `friend_requests` (
    `id`          INT          NOT NULL AUTO_INCREMENT,
    `from_uid`    INT          NOT NULL,
    `from_name`   VARCHAR(64)  NOT NULL,
    `to_uid`      INT          NOT NULL,
    `message`     VARCHAR(256) DEFAULT '',
    `status`      TINYINT      NOT NULL DEFAULT 0,
    `created_at`  DATETIME     DEFAULT CURRENT_TIMESTAMP,
    `updated_at`  DATETIME     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_to_uid` (`to_uid`),
    INDEX `idx_from_uid` (`from_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── 离线消息表（备用：消息持久化）──────────────────────────────────────────
-- type: 0=文字 1=图片 2=文件
CREATE TABLE IF NOT EXISTS `offline_messages` (
    `id`         BIGINT  NOT NULL AUTO_INCREMENT,
    `from_uid`   INT     NOT NULL,
    `to_uid`     INT     NOT NULL,
    `content`    TEXT    NOT NULL,
    `type`       TINYINT NOT NULL DEFAULT 0,
    `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
    `delivered`  TINYINT NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    INDEX `idx_to_uid` (`to_uid`),
    INDEX `idx_delivered` (`delivered`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── 屏蔽用户表 ─────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS `blocked_users` (
    `id`           INT      NOT NULL AUTO_INCREMENT,
    `blocker_uid`  INT      NOT NULL,
    `blocked_uid`  INT      NOT NULL,
    `created_at`   DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_block` (`blocker_uid`, `blocked_uid`),
    INDEX `idx_blocker` (`blocker_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
