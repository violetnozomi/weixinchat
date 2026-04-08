#pragma once
#include <hiredis/hiredis.h>
#include <memory>
#include <string>
#include <atomic>
#include "Singleton.h"
#include "RedisconPool.h"

class RedisMgr : public Singleton<RedisMgr>
{
    friend class Singleton<RedisMgr>;
public:
    ~RedisMgr();

    bool Get(const std::string& key, std::string& value);
    bool Set(const std::string& key, const std::string& value);
    bool SetEx(const std::string& key, const std::string& value, int seconds);
    bool Del(const std::string& key);
    bool ExistsKey(const std::string& key);
    bool Auth(const std::string& password);
    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);
    bool HSet(const std::string& key, const std::string& field, const std::string& value);
    bool HSet(const std::string& key, const std::string& field, int value);
    std::string HGet(const std::string& key, const std::string& field);

    // ── ZSET 操作（供 SyncService Timeline 缓存使用）──────────────────────
    // ZADD key score member
    bool ZAdd(const std::string& key, double score, const std::string& member);
    // ZRANGEBYSCORE key min_score max_score [LIMIT offset count]
    // 返回 score 在 [min_score, +inf) 范围内的 member 列表（按 score ASC）
    std::vector<std::string> ZRangeByScore(const std::string& key,
                                           double min_score,
                                           int    limit = 200);
    // ZCARD key — 返回集合元素数量，key 不存在时返回 0
    long long ZCard(const std::string& key);
    // EXPIRE key seconds — 设置 key 的过期时间
    bool Expire(const std::string& key, int seconds);

    void Close();

private:
    RedisMgr();
    std::unique_ptr<RedisconPool> _pool;
};
