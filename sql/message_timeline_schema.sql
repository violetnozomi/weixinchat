-- ============================================================================
-- Timeline Architecture — 核心消息表 DDL
--
-- 架构理念：消息内容与路由索引分离的写扩散 Timeline 模型
--   ┌─────────────────────────────────────────────────┐
--   │           A 给 B 发一条消息                     │
--   │                                                 │
--   │  message_content   ← 正文存一份                 │
--   │      ↑                                          │
--   │  user_timeline (owner=A, dir=SENT)  ← A 发件箱  │
--   │  user_timeline (owner=B, dir=RECV)  ← B 收件箱  │
--   └─────────────────────────────────────────────────┘
--
-- 核心优势：
--   1. 内容去重：N 个接收者只存 1 份正文，Storage O(1) 而非 O(N)
--   2. 物理局部性：(owner_uid, msg_id) 聚集主键，同一用户所有消息
--                  存储在物理连续的 InnoDB 页上，Range Scan 性能
--                  等同于独立的 per-user 表，而无任何 DDL 代价
--   3. 统一收件箱：消除原来 online/offline 双路径；无论在线与否，
--                  均写 user_timeline，登录/重连时用 msg_id 游标增量同步
--   4. 游标分页：客户端只需持久化 last_sync_id，服务端
--                  "WHERE owner_uid=? AND msg_id > ?" 即可精确增量拉取
-- ============================================================================

USE `weixinchat`;

-- ────────────────────────────────────────────────────────────────────────────
-- message_content  (消息实体表)
--
-- 每条消息的正文全局唯一存储一份。
-- 不包含任何投递状态 —— 投递逻辑属于 user_timeline。
-- msg_id 采用雪花算法生成：
--   Bit [62..22] = ms 时间戳 (41bit)
--   Bit [21..12] = 机器节点 ID (10bit)
--   Bit [11..0]  = 毫秒内序列号 (12bit)
-- 因此 msg_id 自带全局时序，无需额外 ORDER BY created_at 列。
-- ────────────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS `message_content` (

    `msg_id`     BIGINT   NOT NULL
                 COMMENT '雪花 ID：全局唯一 + 时序自带；由 C++ 应用层生成，非 AUTO_INCREMENT',

    `sender_uid` INT      NOT NULL
                 COMMENT '发送者 uid，对应 chat_user.uid；写入后不可变',

    `msg_type`   TINYINT  NOT NULL DEFAULT 0
                 COMMENT '0=文本  1=图片  2=文件  3=语音  9=系统通知',

    `content`    TEXT     NOT NULL
                 COMMENT '消息正文：纯文本 或 结构化 JSON（文件/图片元数据等）',

    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
                 COMMENT '落库时间；时序依据请优先使用 msg_id（毫秒精度），此列供运维直查',

    -- ── 主键：雪花 ID 保证全局唯一且单调递增 ──────────────────────────────
    PRIMARY KEY (`msg_id`),

    -- ── 发件人聚合查询：SELECT * FROM message_content WHERE sender_uid=? ORDER BY msg_id ──
    INDEX `idx_mc_sender` (`sender_uid`, `msg_id`)

) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='消息实体表：每条消息仅存一份正文，由 user_timeline 做写扩散索引';


-- ────────────────────────────────────────────────────────────────────────────
-- user_timeline  (个人信箱 · 漫游同步流)
--
-- 写扩散模型：发消息时在收发双方各插入一条索引行：
--   INSERT (owner=sender, direction=SENT, peer=receiver, msg_id=X)
--   INSERT (owner=receiver, direction=RECV, peer=sender, msg_id=X)
--
-- ── 聚集主键 (owner_uid, msg_id) 的物理效果 ─────────────────────────────────
-- InnoDB 按聚集索引顺序在磁盘上物理排列叶页：
--
--   [磁盘页 A] owner=1001, msg_id=100 | owner=1001, msg_id=101 | ...
--   [磁盘页 B] owner=1002, msg_id=200 | owner=1002, msg_id=201 | ...
--
-- 执行增量同步查询：
--   SELECT ... WHERE owner_uid=1001 AND msg_id > 99 ORDER BY msg_id LIMIT 200
-- MySQL 仅需：B-Tree 一次 seek（O(log N)）+ 顺序读若干连续页（O(M)）
-- 效果等同于独立 per-user 物理表，但不需要 DDL、无文件描述符压力、运维友好。
--
-- ── 辅助索引 idx_ut_conv  ──────────────────────────────────────────────────
-- 支持"查看与某好友的历史记录"场景：
--   SELECT ... WHERE owner_uid=? AND peer_uid=? ORDER BY msg_id DESC LIMIT 50
-- ⚠ MySQL 8.0+ 才支持 DESC 索引列。如使用 5.7，该索引仍能覆盖 WHERE 过滤，
--   但 ORDER BY msg_id DESC 会走 filesort（对 50 条结果可接受）。
-- ────────────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS `user_timeline` (

    `owner_uid`  INT      NOT NULL
                 COMMENT '信箱拥有者 uid；PK 第一列，决定物理聚集方式',

    `msg_id`     BIGINT   NOT NULL
                 COMMENT '雪花 ID；PK 第二列，同时作为时序游标，无需独立 created_at 排序',

    `peer_uid`   INT      NOT NULL
                 COMMENT '聊天对端 uid：发件箱中为接收方，收件箱中为发送方',

    `direction`  TINYINT  NOT NULL DEFAULT 1
                 COMMENT '1=RECV（收到该消息）  2=SENT（发出该消息）',

    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
                 COMMENT '落库时间；仅供运维排查，业务时序请使用 msg_id',

    -- ── 聚集主键：保证同一用户的 Timeline 行物理连续 ──────────────────────
    PRIMARY KEY (`owner_uid`, `msg_id`),

    -- ── 辅助索引：按对端过滤 + 倒序翻页（MySQL 8.0+ 可利用 DESC）─────────
    INDEX `idx_ut_conv` (`owner_uid`, `peer_uid`, `msg_id`)

    -- ── 注：不添加 FK(msg_id) → message_content ──────────────────────────
    -- 生产级高并发表上 FK 约束会引入行级写锁广播，影响写入 TPS；
    -- 一致性由应用层事务保证（saveMessage 内 startTransaction/commit）。

) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='个人信箱同步流：写扩散，收发各一条索引；聚集 PK 保障增量拉取 Range Scan 性能';
