// SyncService.cpp
#include "SyncService.h"
#include "RedisMgr.h"
#include "MySqlPool.h"
#include "DbWriteQueue.h"
#include "SnowflakeID.h"
#include <json/json.h>
#include <iostream>
#include <sstream>

using namespace mysqlx;

// ── out-of-line 定义 static constexpr 成员 ─────────────────────────────────────
// C++14 要求：如果 constexpr 常量在表达式中被 ODR-used（如 lambda 内传参）
// 必须在 .cpp 里提供一处定义。C++17 inline 变量无此限制。
constexpr const char* SyncService::kNullSentinel;
constexpr int         SyncService::kNullTtlSec;
constexpr int         SyncService::kCacheTtlSec;
constexpr int         SyncService::kMaxPullCount;

// ─────────────────────────────────────────────────────────────────────────────
// 序列化 / 反序列化
// ─────────────────────────────────────────────────────────────────────────────

/*static*/
std::string SyncService::serializeMessage(const Message& msg) {
    Json::Value v;
    v["i"] = static_cast<Json::Int64>(msg.msg_id);
    v["s"] = msg.sender_uid;
    v["p"] = msg.peer_uid;
    v["t"] = msg.msg_type;
    v["d"] = msg.direction;
    v["c"] = msg.content;
    v["ts"] = static_cast<Json::Int64>(msg.created_at_ms);
    Json::FastWriter fw;
    auto s = fw.write(v);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

/*static*/
Message SyncService::deserializeMessage(const std::string& json) {
    Message msg;
    Json::Value v;
    Json::Reader r;
    if (!r.parse(json, v)) return msg;
    msg.msg_id        = v["i"].asInt64();
    msg.sender_uid    = v["s"].asInt();
    msg.peer_uid      = v["p"].asInt();
    msg.msg_type      = static_cast<int8_t>(v["t"].asInt());
    msg.direction     = static_cast<int8_t>(v["d"].asInt());
    msg.content       = v["c"].asString();
    msg.created_at_ms = v["ts"].asInt64();
    return msg;
}

// ─────────────────────────────────────────────────────────────────────────────
// per-uid 互斥锁管理（防缓存击穿）
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<std::mutex> SyncService::getOrCreateMutex(int uid) {
    std::lock_guard<std::mutex> lk(_map_mutex);
    auto it = _uid_mutexes.find(uid);
    if (it != _uid_mutexes.end()) return it->second;
    auto m = std::make_shared<std::mutex>();
    _uid_mutexes[uid] = m;
    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
// Redis 查询
// ─────────────────────────────────────────────────────────────────────────────

std::vector<Message> SyncService::queryRedisTimeline(const std::string& redis_key,
                                                      uint64_t since_id) {
    // score = msg_id >> 22（雪花高 41 位 ms 时间戳），score 单调严格递增
    double min_score = static_cast<double>(since_id >> 22);
    auto raw = RedisMgr::GetInstance()->ZRangeByScore(redis_key, min_score, kMaxPullCount);

    std::vector<Message> result;
    for (auto& s : raw) {
        // ── 防缓存穿透哑元：跳过占位符 ──────────────────────────────────────
        if (s == kNullSentinel) continue;

        auto msg = deserializeMessage(s);

        // ── 精确过滤：score 是 ms 级，同一毫秒内可能有多条，需用雪花 ID 精确比较 ──
        if (static_cast<uint64_t>(msg.msg_id) <= since_id) continue;

        result.push_back(std::move(msg));
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// MySQL 查询
// ─────────────────────────────────────────────────────────────────────────────

bool SyncService::queryTimelineFromDB(int owner_uid, uint64_t since_id,
                                       std::vector<TimelineEntry>& out) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        // 利用 (owner_uid, msg_id) 聚集主键做精确 Range Scan
        // ⚠ msg_id 是 BIGINT，since_id 是 uint64；强转 int64 安全（雪花 ID 最高位恒为 0）
        SqlResult r = guard.get()->sql(
            "SELECT msg_id, peer_uid, direction "
            "FROM user_timeline "
            "WHERE owner_uid = ? AND msg_id > ? "
            "ORDER BY msg_id ASC "
            "LIMIT ?")
            .bind(owner_uid)
            .bind(static_cast<int64_t>(since_id))
            .bind(kMaxPullCount)
            .execute();

        for (Row row : r.fetchAll()) {
            TimelineEntry e;
            e.msg_id    = static_cast<int64_t>(row[0]);
            e.peer_uid  = static_cast<int>(row[1]);
            e.direction = static_cast<int8_t>(static_cast<int>(row[2]));
            out.push_back(e);
        }
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[SyncService] queryTimelineFromDB error: " << ex.what() << "\n";
        return false;
    }
}

bool SyncService::batchGetMessageContent(
    const std::vector<int64_t>& msg_ids,
    std::unordered_map<int64_t, MessageContent>& out)
{
    if (msg_ids.empty()) return true;
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        // 构造 IN (?,?,?) 参数化占位符列表（防 SQL 注入）
        std::ostringstream oss;
        oss << "SELECT msg_id, sender_uid, msg_type, content "
               "FROM message_content WHERE msg_id IN (";
        for (size_t i = 0; i < msg_ids.size(); ++i) {
            oss << (i ? "," : "") << "?";
        }
        oss << ")";

        auto stmt = guard.get()->sql(oss.str());
        for (const auto& id : msg_ids) {
            stmt.bind(id);
        }
        SqlResult r = stmt.execute();
        for (Row row : r.fetchAll()) {
            MessageContent mc;
            mc.msg_id     = static_cast<int64_t>(row[0]);
            mc.sender_uid = static_cast<int>(row[1]);
            mc.msg_type   = static_cast<int8_t>(static_cast<int>(row[2]));
            mc.content    = row[3].get<std::string>();
            out[mc.msg_id] = std::move(mc);
        }
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[SyncService] batchGetMessageContent error: " << ex.what() << "\n";
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 缓存回写（异步投递到 DbWriteQueue，不阻塞调用线程）
// ─────────────────────────────────────────────────────────────────────────────

void SyncService::rebuildRedisCache(const std::string& redis_key,
                                     const std::vector<Message>& msgs) {
    if (msgs.empty()) return;
    // 捕获副本，安全投递到异步队列
    // 用局部常量传入 lambda，避免 ODR-used 连接问题
    const int ttl = kCacheTtlSec;
    DbWriteQueue::GetInstance()->PostWrite([redis_key, msgs, ttl]() {
        auto* redis = RedisMgr::GetInstance();
        for (const auto& msg : msgs) {
            double score = static_cast<double>(
                static_cast<uint64_t>(msg.msg_id) >> 22);
            redis->ZAdd(redis_key, score, SyncService::serializeMessage(msg));
        }
        // 刷新 TTL，避免热点用户的缓存在活跃期间被淘汰
        redis->Expire(redis_key, ttl);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// pullSyncMessages  ── 核心入口
// ─────────────────────────────────────────────────────────────────────────────

std::vector<Message> SyncService::pullSyncMessages(int owner_uid,
                                                    uint64_t client_last_sync_id) {
    const std::string redis_key = "timeline:" + std::to_string(owner_uid);

    // ══ L1：优先查 Redis ZSET ════════════════════════════════════════════════
    {
        long long card = RedisMgr::GetInstance()->ZCard(redis_key);
        if (card > 0) {
            auto cached = queryRedisTimeline(redis_key, client_last_sync_id);

            if (!cached.empty()) {
                // ── 缓存命中，刷新 TTL（sliding window）──────────────────
                RedisMgr::GetInstance()->Expire(redis_key, kCacheTtlSec);
                return cached;
            }
            // card > 0 但过滤后为空：
            //   情况 A：全部缓存条目 msg_id ≤ since_id，用户已是最新状态
            //   情况 B：唯一条目是哑元（queryRedisTimeline 内部已过滤），确实无消息
            //   两种情况均不需走 DB
            return {};
        }
    }

    // ══ L2：Cache Miss — 防缓存击穿：per-uid 互斥锁保证只有一个线程回源 ═════
    auto uid_mtx = getOrCreateMutex(owner_uid);
    std::lock_guard<std::mutex> breakdown_guard(*uid_mtx);

    // Double-Check：持锁后再查一次 Redis，避免前一个线程已重建缓存后重复查 DB
    {
        long long card = RedisMgr::GetInstance()->ZCard(redis_key);
        if (card > 0) {
            auto cached = queryRedisTimeline(redis_key, client_last_sync_id);
            if (!cached.empty()) return cached;
            if (card > 0 && cached.empty()) return {};
        }
    }

    // ══ 回源 MySQL ═══════════════════════════════════════════════════════════

    // Step 1：从 user_timeline 拉索引（聚集 PK Range Scan）
    std::vector<TimelineEntry> entries;
    if (!queryTimelineFromDB(owner_uid, client_last_sync_id, entries)) {
        return {}; // DB 故障，降级返回空，客户端可稍后重试
    }

    // ── 防缓存穿透：DB 查完仍为空，写哑元 + 短 TTL ──────────────────────────
    if (entries.empty()) {
        const int null_ttl = kNullTtlSec;
        DbWriteQueue::GetInstance()->PostWrite([redis_key, null_ttl]() {
            // score=0 的哑元，ZRANGEBYSCORE 能查到但业务层跳过
            RedisMgr::GetInstance()->ZAdd(redis_key, 0.0, kNullSentinel);
            RedisMgr::GetInstance()->Expire(redis_key, null_ttl);
        });
        return {};
    }

    // Step 2：收集 msg_id 列表，批量拉取消息正文（N+1 → 1 次 Batch SQL）
    std::vector<int64_t> msg_id_list;
    msg_id_list.reserve(entries.size());
    for (const auto& e : entries) msg_id_list.push_back(e.msg_id);

    std::unordered_map<int64_t, MessageContent> content_map;
    if (!batchGetMessageContent(msg_id_list, content_map)) {
        return {};
    }

    // Step 3：拼装完整 Message 对象
    std::vector<Message> result;
    result.reserve(entries.size());
    for (const auto& e : entries) {
        auto it = content_map.find(e.msg_id);
        if (it == content_map.end()) continue; // 正文缺失（数据异常），跳过

        Message msg;
        msg.msg_id        = e.msg_id;
        msg.sender_uid    = it->second.sender_uid;
        msg.peer_uid      = e.peer_uid;
        msg.msg_type      = it->second.msg_type;
        msg.direction     = e.direction;
        msg.content       = it->second.content;
        // 从雪花 ID 高 41 位解码毫秒时间戳
        msg.created_at_ms = SnowflakeID::ExtractTimestampMs(e.msg_id);
        result.push_back(std::move(msg));
    }

    // Step 4：异步回写 Redis，恢复缓存（不阻塞当前调用线程）
    rebuildRedisCache(redis_key, result);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// saveMessage  ── 写扩散落库（在 DbWriteQueue 工作线程中调用）
// ─────────────────────────────────────────────────────────────────────────────

bool SyncService::saveMessage(int sender_uid, int receiver_uid, int8_t msg_type,
                               const std::string& content, int64_t& out_msg_id) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const int64_t msg_id = NewSnowflakeID();
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();

        guard.get()->startTransaction();

        // ① 消息正文存一份
        guard.get()->sql(
            "INSERT INTO `" + schema + "`.`message_content` "
            "(msg_id, sender_uid, msg_type, content) VALUES (?,?,?,?)")
            .bind(msg_id)
            .bind(sender_uid)
            .bind(static_cast<int>(msg_type))
            .bind(content)
            .execute();

        // ② 发件人 Timeline（方向=SENT）
        guard.get()->sql(
            "INSERT INTO `" + schema + "`.`user_timeline` "
            "(owner_uid, msg_id, peer_uid, direction) VALUES (?,?,?,2)")
            .bind(sender_uid)
            .bind(msg_id)
            .bind(receiver_uid)
            .execute();

        // ③ 收件人 Timeline（方向=RECV）
        guard.get()->sql(
            "INSERT INTO `" + schema + "`.`user_timeline` "
            "(owner_uid, msg_id, peer_uid, direction) VALUES (?,?,?,1)")
            .bind(receiver_uid)
            .bind(msg_id)
            .bind(sender_uid)
            .execute();

        guard.get()->commit();
        out_msg_id = msg_id;

        // ④ 异步更新双方 Redis ZSET（事务提交后再更新，保证缓存不超前于 DB）
        Message sent_copy, recv_copy;
        sent_copy.msg_id = recv_copy.msg_id = msg_id;
        sent_copy.sender_uid = recv_copy.sender_uid = sender_uid;
        sent_copy.msg_type   = recv_copy.msg_type   = msg_type;
        sent_copy.content    = recv_copy.content    = content;
        sent_copy.created_at_ms = recv_copy.created_at_ms =
            SnowflakeID::ExtractTimestampMs(msg_id);
        sent_copy.peer_uid  = receiver_uid;  sent_copy.direction = 2;
        recv_copy.peer_uid  = sender_uid;    recv_copy.direction = 1;

        double score = static_cast<double>(
            static_cast<uint64_t>(msg_id) >> 22);

        std::string sender_key   = "timeline:" + std::to_string(sender_uid);
        std::string receiver_key = "timeline:" + std::to_string(receiver_uid);
        std::string sent_json    = serializeMessage(sent_copy);
        std::string recv_json    = serializeMessage(recv_copy);

        const int cache_ttl = kCacheTtlSec;
        DbWriteQueue::GetInstance()->PostWrite(
            [sender_key, receiver_key, score, sent_json, recv_json, cache_ttl]() {
                auto* redis = RedisMgr::GetInstance();
                redis->ZAdd(sender_key,   score, sent_json);
                redis->ZAdd(receiver_key, score, recv_json);
                redis->Expire(sender_key,   cache_ttl);
                redis->Expire(receiver_key, cache_ttl);
            });

        return true;
    } catch (const std::exception& ex) {
        try { guard.get()->rollback(); } catch (...) {}
        std::cerr << "[SyncService] saveMessage error: " << ex.what() << "\n";
        return false;
    }
}