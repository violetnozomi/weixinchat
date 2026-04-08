#pragma once
// ============================================================================
// SyncService — 漫游消息拉取 & Timeline 写扩散服务
//
// ── Redis 缓存结构 ───────────────────────────────────────────────────────────
//   Key    : "timeline:{owner_uid}"
//   Type   : ZSET
//   Score  : msg_id >> 22  （雪花 ID 高 41 位 ms 时间戳，uint64 整数精确存储）
//   Member : JSON 序列化的 Message（字段缩写以节省 Redis 内存）
//
// ── 线程模型 ──────────────────────────────────────────────────────────────────
//   pullSyncMessages  ——  在 DbReadQueue 工作线程中调用（含同步 MySQL 查询）
//   saveMessage       ——  在 DbWriteQueue 工作线程中调用（含同步 MySQL 写入）
//   缓存回写 (ZADD)   ——  异步投递到 DbWriteQueue，不阻塞调用线程
//
// ── 典型调用方（登录/重连 handler，运行在 ChatLogicSystem 逻辑线程池）────────
//
//   DbReadQueue::GetInstance()->PostRead([uid, last_sync_id, session]() {
//       auto msgs = SyncService::GetInstance()
//                       ->pullSyncMessages(uid, last_sync_id);
//       // 结果回投到逻辑线程
//       boost::asio::post(*ChatLogicSystem::GetInstance()->GetPool(),
//           [session, msgs]() { session->Send(ID_SYNC_MSG, toJson(msgs)); });
//   });
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "Singleton.h"

// ── 完整消息对象（拼装后返回给上层调用者）──────────────────────────────────
struct Message {
    int64_t     msg_id        = 0;
    int         sender_uid    = 0;
    int         peer_uid      = 0;   // 聊天对端 uid（来自 user_timeline.peer_uid）
    int8_t      msg_type      = 0;   // 0=文本 1=图片 2=文件 3=语音 9=系统通知
    int8_t      direction     = 0;   // 1=RECV（收到） 2=SENT（发出）
    std::string content;
    int64_t     created_at_ms = 0;   // 从雪花 ID 解码出的毫秒时间戳
};

// ── MySQL 中间结果：user_timeline 索引行 ────────────────────────────────────
struct TimelineEntry {
    int64_t msg_id    = 0;
    int     peer_uid  = 0;
    int8_t  direction = 0;
};

// ── MySQL 中间结果：message_content 内容行 ──────────────────────────────────
struct MessageContent {
    int64_t     msg_id     = 0;
    int         sender_uid = 0;
    int8_t      msg_type   = 0;
    std::string content;
};

// ─────────────────────────────────────────────────────────────────────────────
class SyncService : public Singleton<SyncService> {
    friend class Singleton<SyncService>;

public:
    // ── 核心 API 1：拉取漫游/离线消息 ────────────────────────────────────────
    //
    // 优先命中 Redis ZSET（L1 缓存，< 1ms）；
    // Cache Miss 后以 per-uid 互斥锁防止击穿，降级查 MySQL（L2），
    // 异步回写 Redis，并对真实空结果写哑元防穿透。
    //
    // @param owner_uid          信箱拥有者 uid
    // @param client_last_sync_id 客户端已同步到的最后一条消息的雪花 ID（0=首次登录）
    // @return                    msg_id > client_last_sync_id 的消息列表，按 msg_id ASC
    std::vector<Message> pullSyncMessages(int owner_uid, uint64_t client_last_sync_id);

    // ── 核心 API 2：持久化一条消息（内容 + 双向 Timeline 写扩散） ────────────
    //
    // 在同一个 MySQL 事务内：
    //   1. INSERT message_content（正文存一份）
    //   2. INSERT user_timeline（sender，direction=SENT）
    //   3. INSERT user_timeline（receiver，direction=RECV）
    // 事务提交后，异步 ZADD 更新双方 Redis ZSET。
    //
    // @param out_msg_id  输出参数：成功时填入生成的雪花 ID
    bool saveMessage(int sender_uid, int receiver_uid, int8_t msg_type,
                     const std::string& content, int64_t& out_msg_id);

    // ── 序列化工具 (static，可在 DbWriteQueue lambda 中直接调用) ─────────────
    static std::string serializeMessage(const Message& msg);
    static Message     deserializeMessage(const std::string& json);

    // ── Score 计算：取雪花 ID 高 41 位（ms 时间戳），安全映射到 double ────────
    // 最大值 2^41 ≈ 2.2×10^12，远小于 double 精确整数上限 2^53，无精度损失。
    static uint64_t toTimelineScore(int64_t msg_id) {
        return static_cast<uint64_t>(msg_id) >> 22;
    }

private:
    SyncService() = default;

    // ── Redis 操作 ────────────────────────────────────────────────────────────
    std::vector<Message> queryRedisTimeline(const std::string& redis_key,
                                             uint64_t since_id);
    void rebuildRedisCache(const std::string& redis_key,
                           const std::vector<Message>& msgs);

    // ── MySQL 操作 ────────────────────────────────────────────────────────────
    bool queryTimelineFromDB(int owner_uid, uint64_t since_id,
                             std::vector<TimelineEntry>& out);
    bool batchGetMessageContent(const std::vector<int64_t>& msg_ids,
                                std::unordered_map<int64_t, MessageContent>& out);

    // ── 防缓存击穿：per-uid 重建锁 ───────────────────────────────────────────
    // ⚠ 此 map 无上界淘汰；生产环境建议改用 sharded-lru-mutex（如 abseil 的版本）
    std::shared_ptr<std::mutex> getOrCreateMutex(int uid);
    std::mutex                                        _map_mutex;
    std::unordered_map<int, std::shared_ptr<std::mutex>> _uid_mutexes;

    // ── 常量 ──────────────────────────────────────────────────────────────────
    static constexpr const char* kNullSentinel = "__null__"; // 防穿透哑元 member
    static constexpr int         kNullTtlSec   = 30;         // 哑元过期时间（秒）
    static constexpr int         kCacheTtlSec  = 7 * 24 * 3600; // 正常缓存 TTL（7天）
    static constexpr int         kMaxPullCount = 200;         // 单次最多拉取条数
};
