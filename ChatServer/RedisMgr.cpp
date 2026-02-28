#include "RedisMgr.h"
#include "ConfigMgr.h"
#include <iostream>

// RAII guard: borrows a connection from the pool and returns it automatically
struct RedisConnectionGuard {
    RedisconPool* pool;
    redisContext* ctx;
    explicit RedisConnectionGuard(RedisconPool* p) : pool(p), ctx(p->getRedisContext()) {}
    ~RedisConnectionGuard() { if (ctx) pool->returnContext(ctx); }
    bool valid() const { return ctx != nullptr; }
};

RedisMgr::RedisMgr() {
    auto& cfg = ConfigMgr::Inst();
    auto host = cfg["Redis"]["Host"];
    auto port = std::stoi(cfg["Redis"]["Port"]);
    auto pwd  = cfg["Redis"]["Password"];
    _pool = std::make_unique<RedisconPool>(5, host.c_str(), port, pwd.c_str());
}

RedisMgr::~RedisMgr() {
    Close();
}

void RedisMgr::Close() {
    if (_pool) _pool->close();
}

bool RedisMgr::Auth(const std::string& password) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "AUTH %s", password.c_str());
    if (!r) return false;
    bool ok = (r->type != REDIS_REPLY_ERROR);
    freeReplyObject(r);
    return ok;
}

bool RedisMgr::Get(const std::string& key, std::string& value) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "GET %s", key.c_str());
    if (!r) return false;
    if (r->type == REDIS_REPLY_NIL) { freeReplyObject(r); return false; }
    if (r->type != REDIS_REPLY_STRING) { freeReplyObject(r); return false; }
    value = r->str;
    freeReplyObject(r);
    return true;
}

bool RedisMgr::Set(const std::string& key, const std::string& value) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "SET %s %s", key.c_str(), value.c_str());
    if (!r) return false;
    bool ok = (r->type == REDIS_REPLY_STATUS &&
               std::string(r->str) == "OK");
    freeReplyObject(r);
    return ok;
}

bool RedisMgr::SetEx(const std::string& key, const std::string& value, int seconds) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "SETEX %s %d %s",
                                       key.c_str(), seconds, value.c_str());
    if (!r) return false;
    bool ok = (r->type == REDIS_REPLY_STATUS &&
               std::string(r->str) == "OK");
    freeReplyObject(r);
    return ok;
}

bool RedisMgr::Del(const std::string& key) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "DEL %s", key.c_str());
    if (!r) return false;
    freeReplyObject(r);
    return true;
}

bool RedisMgr::ExistsKey(const std::string& key) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "EXISTS %s", key.c_str());
    if (!r) return false;
    bool ok = (r->type == REDIS_REPLY_INTEGER && r->integer > 0);
    freeReplyObject(r);
    return ok;
}

bool RedisMgr::LPush(const std::string& key, const std::string& value) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "LPUSH %s %s", key.c_str(), value.c_str());
    if (!r) return false;
    bool ok = (r->type == REDIS_REPLY_INTEGER);
    freeReplyObject(r);
    return ok;
}

bool RedisMgr::LPop(const std::string& key, std::string& value) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "LPOP %s", key.c_str());
    if (!r) return false;
    if (r->type == REDIS_REPLY_NIL) { freeReplyObject(r); return false; }
    if (r->type != REDIS_REPLY_STRING) { freeReplyObject(r); return false; }
    value = r->str;
    freeReplyObject(r);
    return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "RPUSH %s %s", key.c_str(), value.c_str());
    if (!r) return false;
    bool ok = (r->type == REDIS_REPLY_INTEGER);
    freeReplyObject(r);
    return ok;
}

bool RedisMgr::RPop(const std::string& key, std::string& value) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "RPOP %s", key.c_str());
    if (!r) return false;
    if (r->type == REDIS_REPLY_NIL) { freeReplyObject(r); return false; }
    if (r->type != REDIS_REPLY_STRING) { freeReplyObject(r); return false; }
    value = r->str;
    freeReplyObject(r);
    return true;
}

bool RedisMgr::HSet(const std::string& key, const std::string& field,
                    const std::string& value) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return false;
    auto r = (redisReply*)redisCommand(g.ctx, "HSET %s %s %s",
                                       key.c_str(), field.c_str(), value.c_str());
    if (!r) return false;
    bool ok = (r->type == REDIS_REPLY_INTEGER);
    freeReplyObject(r);
    return ok;
}

bool RedisMgr::HSet(const std::string& key, const std::string& field, int value) {
    return HSet(key, field, std::to_string(value));
}

std::string RedisMgr::HGet(const std::string& key, const std::string& field) {
    RedisConnectionGuard g(_pool.get());
    if (!g.valid()) return "";
    auto r = (redisReply*)redisCommand(g.ctx, "HGET %s %s", key.c_str(), field.c_str());
    if (!r) return "";
    if (r->type != REDIS_REPLY_STRING) { freeReplyObject(r); return ""; }
    std::string val = r->str;
    freeReplyObject(r);
    return val;
}
