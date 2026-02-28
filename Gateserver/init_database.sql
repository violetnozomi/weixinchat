-- ========================================
-- 聊天系统数据库初始化脚本
-- 使用 wxchat 数据库
-- ========================================

-- 使用 wxchat 数据库
USE `wxchat`;

-- 1. 删除旧表（如果需要重新创建）
DROP TABLE IF EXISTS `chat_user`;
DROP TABLE IF EXISTS `user_id`;

-- 2. 创建用户表
CREATE TABLE IF NOT EXISTS `chat_user` (
    `id` INT AUTO_INCREMENT PRIMARY KEY COMMENT '自增主键',
    `uid` INT UNIQUE NOT NULL COMMENT '用户ID',
    `name` VARCHAR(50) UNIQUE NOT NULL COMMENT '用户名',
    `email` VARCHAR(100) UNIQUE NOT NULL COMMENT '邮箱',
    `pwd` VARCHAR(255) NOT NULL COMMENT '密码',
    `signature` VARCHAR(256) DEFAULT '' COMMENT '个性签名',
    `avatar_path` VARCHAR(512) DEFAULT '' COMMENT '头像路径',
    `create_time` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    INDEX idx_email (`email`),
    INDEX idx_name (`name`),
    INDEX idx_uid (`uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户表';

-- 3. 创建ID生成器表
CREATE TABLE IF NOT EXISTS `user_id` (
    `id` INT PRIMARY KEY DEFAULT 1000 COMMENT '当前可用的用户ID'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户ID生成器';

-- 4. 初始化ID生成器（如果表为空才插入）
INSERT INTO `user_id` (`id`) 
SELECT 1000 
WHERE NOT EXISTS (SELECT 1 FROM `user_id` LIMIT 1);

-- 5. 查看表结构
SHOW TABLES LIKE 'chat_user';
SHOW TABLES LIKE 'user_id';

DESC chat_user;
DESC user_id;

-- 6. 查看当前数据
SELECT * FROM user_id;
SELECT COUNT(*) as user_count FROM chat_user;

-- 7. 测试查询
SELECT 'Tables created successfully!' as Status;
