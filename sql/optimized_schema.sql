-- Optimized schema for weixinchat (aligned with existing tables)
-- Notes:
-- 1) Uses uid as primary key for chat_user.
-- 2) Adds timestamps + indexes for common queries.
-- 3) Keeps existing table names to minimize code changes.

CREATE DATABASE IF NOT EXISTS `weixinchat` DEFAULT CHARSET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `weixinchat`;

-- ----------------------------
-- chat_user
-- ----------------------------
CREATE TABLE IF NOT EXISTS `chat_user` (
    `uid`         INT          NOT NULL AUTO_INCREMENT,
    `name`        VARCHAR(64)  NOT NULL,
    `email`       VARCHAR(128) NOT NULL,
    `pwd`         VARCHAR(256) NOT NULL,
    `signature`   VARCHAR(256) DEFAULT '',
    `avatar_path` VARCHAR(512) DEFAULT '',
    `created_at`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`uid`),
    UNIQUE KEY `uq_user_name` (`name`),
    UNIQUE KEY `uq_user_email` (`email`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- chat_friends (bidirectional)
-- ----------------------------
CREATE TABLE IF NOT EXISTS `chat_friends` (
    `user_uid`   INT      NOT NULL,
    `friend_uid` INT      NOT NULL,
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`user_uid`, `friend_uid`),
    INDEX `idx_friend_uid` (`friend_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- friend_requests
-- status: 0=pending, 1=accepted, 2=rejected, 3=expired
-- ----------------------------
CREATE TABLE IF NOT EXISTS `friend_requests` (
    `id`         BIGINT       NOT NULL,   -- 由应用层雪花算法生成
    `from_uid`   INT          NOT NULL,
    `from_name`  VARCHAR(64)  NOT NULL,
    `to_uid`     INT          NOT NULL,
    `message`    VARCHAR(256) DEFAULT '',
    `status`     TINYINT      NOT NULL DEFAULT 0,
    `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_req_from_to` (`from_uid`, `to_uid`),
    INDEX `idx_req_to_uid` (`to_uid`),
    INDEX `idx_req_from_uid` (`from_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- chat_messages
-- ----------------------------
CREATE TABLE IF NOT EXISTS `chat_messages` (
    `id`         BIGINT       NOT NULL,   -- 由应用层雪花算法生成
    `from_uid`   INT          NOT NULL,
    `to_uid`     INT          NOT NULL,
    `content`    TEXT         NOT NULL,
    `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_conv_forward`  (`from_uid`, `to_uid`, `created_at`),
    INDEX `idx_conv_backward` (`to_uid`, `from_uid`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- offline_messages
-- type: 0=text, 1=image, 2=file, 3=voice
-- ----------------------------
CREATE TABLE IF NOT EXISTS `offline_messages` (
    `id`          BIGINT   NOT NULL,   -- 由应用层雪花算法生成
    `from_uid`    INT      NOT NULL,
    `to_uid`      INT      NOT NULL,
    `content`     TEXT     NOT NULL,
    `type`        TINYINT  NOT NULL DEFAULT 0,
    `created_at`  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `delivered`   TINYINT  NOT NULL DEFAULT 0,
    `delivered_at` DATETIME NULL DEFAULT NULL,
    PRIMARY KEY (`id`),
    INDEX `idx_offline_to` (`to_uid`, `delivered`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- file_transfers (MinIO)
-- ----------------------------
CREATE TABLE IF NOT EXISTS `file_transfers` (
    `id`           BIGINT       NOT NULL,   -- 由应用层雪花算法生成
    `sender_uid`   INT          NOT NULL,
    `receiver_uid` INT          NOT NULL,
    `file_key`     VARCHAR(512) NOT NULL,
    `filename`     VARCHAR(255) NOT NULL,
    `file_size`    BIGINT       NOT NULL,
    `download_url` TEXT         NOT NULL,
    `is_image`     TINYINT      NOT NULL DEFAULT 0,
    `is_voice`     TINYINT      NOT NULL DEFAULT 0,
    `duration_ms`  INT          NOT NULL DEFAULT 0,
    `delivered`    TINYINT      NOT NULL DEFAULT 0,
    `created_at`   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_file_key` (`file_key`),
    INDEX `idx_file_receiver` (`receiver_uid`, `delivered`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- blocked_users
-- ----------------------------
CREATE TABLE IF NOT EXISTS `blocked_users` (
    `blocker_uid` INT      NOT NULL,
    `blocked_uid` INT      NOT NULL,
    `created_at`  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`blocker_uid`, `blocked_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- chat_group
-- status: 1=normal, 2=dissolved
-- ----------------------------
CREATE TABLE IF NOT EXISTS `chat_group` (
    `group_id`    BIGINT       NOT NULL,
    `owner_uid`   INT          NOT NULL,
    `group_name`  VARCHAR(128) NOT NULL,
    `avatar_path` VARCHAR(512) DEFAULT '',
    `notice`      VARCHAR(1024) DEFAULT '',
    `status`      TINYINT      NOT NULL DEFAULT 1,
    `created_at`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`group_id`),
    INDEX `idx_group_owner` (`owner_uid`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- chat_group_member
-- role: 0=owner, 1=admin, 2=member
-- status: 1=active, 2=quit, 3=kicked
-- ----------------------------
CREATE TABLE IF NOT EXISTS `chat_group_member` (
    `group_id`   BIGINT   NOT NULL,
    `uid`        INT      NOT NULL,
    `role`       TINYINT  NOT NULL DEFAULT 2,
    `status`     TINYINT  NOT NULL DEFAULT 1,
    `mute_until` DATETIME NULL DEFAULT NULL,
    `joined_at`  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`group_id`, `uid`),
    INDEX `idx_group_member_uid` (`uid`, `status`),
    INDEX `idx_group_member_role` (`group_id`, `role`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- group_message_content
-- msg_type: 0=text, 1=image, 2=file, 3=voice, 9=system
-- ----------------------------
CREATE TABLE IF NOT EXISTS `group_message_content` (
    `msg_id`      BIGINT   NOT NULL,
    `group_id`    BIGINT   NOT NULL,
    `sender_uid`  INT      NOT NULL,
    `msg_type`    TINYINT  NOT NULL DEFAULT 0,
    `content`     TEXT     NOT NULL,
    `created_at`  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`msg_id`),
    INDEX `idx_group_msg` (`group_id`, `msg_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- group_user_timeline
-- ----------------------------
CREATE TABLE IF NOT EXISTS `group_user_timeline` (
    `owner_uid`   INT      NOT NULL,
    `group_id`    BIGINT   NOT NULL,
    `msg_id`      BIGINT   NOT NULL,
    `created_at`  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`owner_uid`, `msg_id`),
    INDEX `idx_group_conv` (`owner_uid`, `group_id`, `msg_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- group_read_state
-- ----------------------------
CREATE TABLE IF NOT EXISTS `group_read_state` (
    `group_id`          BIGINT   NOT NULL,
    `uid`               INT      NOT NULL,
    `last_read_msg_id`  BIGINT   NOT NULL DEFAULT 0,
    `last_read_at`      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`group_id`, `uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- user_conversation
-- conv_type: 1=peer, 2=group
-- ----------------------------
CREATE TABLE IF NOT EXISTS `user_conversation` (
    `owner_uid`    INT      NOT NULL,
    `conv_type`    TINYINT  NOT NULL,
    `conv_id`      BIGINT   NOT NULL,
    `last_msg_id`  BIGINT   NOT NULL DEFAULT 0,
    `unread_count` INT      NOT NULL DEFAULT 0,
    `is_pinned`    TINYINT  NOT NULL DEFAULT 0,
    `is_muted`     TINYINT  NOT NULL DEFAULT 0,
    `is_hidden`    TINYINT  NOT NULL DEFAULT 0,
    `updated_at`   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`owner_uid`, `conv_type`, `conv_id`),
    INDEX `idx_user_conv_recent` (`owner_uid`, `updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- user_id (optional legacy UID allocator)
-- Keep only if your code still relies on it.
-- ----------------------------
CREATE TABLE IF NOT EXISTS `user_id` (
    `id` INT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
