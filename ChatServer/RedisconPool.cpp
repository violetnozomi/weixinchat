#include "RedisconPool.h"
#include <iostream>
#include <chrono>

RedisconPool::RedisconPool(std::size_t poolSize, const char* host, int port, const char* pwd)
    : _poolSize(poolSize), _host(host), _port(port), _pwd(pwd), _b_stop(false)
{
    std::cout<<_port<<std::endl;
    for (std::size_t i = 0; i < poolSize; ++i) {
        redisContext* ctx = redisConnect(_host.c_str(), _port);
        if (!ctx || ctx->err) {
            if (ctx) redisFree(ctx);
            std::cerr << "[Redis] connect failed (slot " << i << ")" << std::endl;
            continue;
        }
        if (!_pwd.empty()) {
            auto reply = (redisReply*)redisCommand(ctx, "AUTH %s", _pwd.c_str());
            std::cout<<_pwd<<std::endl;
            std::cout << reply->str << std::endl;
            if (!reply || reply->type == REDIS_REPLY_ERROR) {
                if (reply) freeReplyObject(reply);
                redisFree(ctx);
                std::cerr << "[Redis] AUTH failed (slot " << i << ")" << std::endl;
                continue;
            }
            freeReplyObject(reply);
        }
        _redisContexts.push(ctx);
    }
    std::cout << "[Redis] Pool ready: " << _redisContexts.size()
              << "/" << poolSize << " connections" << std::endl;
}

RedisconPool::~RedisconPool() {
    std::lock_guard<std::mutex> lk(_mutex);
    while (!_redisContexts.empty()) {
        redisFree(_redisContexts.front());
        _redisContexts.pop();
    }
}

redisContext* RedisconPool::getRedisContext() {
    std::unique_lock<std::mutex> lk(_mutex);
    bool ok = _condition.wait_for(lk, std::chrono::seconds(2),
                                  [this]{ return _b_stop || !_redisContexts.empty(); });
    if (!ok || _b_stop) return nullptr;   // timed out or shutting down
    auto ctx = _redisContexts.front();
    _redisContexts.pop();
    return ctx;
}

void RedisconPool::returnContext(redisContext* ctx) {
    std::lock_guard<std::mutex> lk(_mutex);
    if (_b_stop) { redisFree(ctx); return; }
    if (!checkConnection(ctx)) {
        redisFree(ctx);
        // 尝试重建一条连接
        redisContext* nctx = redisConnect(_host.c_str(), _port);
        if (nctx && !nctx->err) {
            if (!_pwd.empty()) {
                auto r = (redisReply*)redisCommand(nctx, "AUTH %s", _pwd.c_str());
                if (!r || r->type == REDIS_REPLY_ERROR) {
                    if (r) freeReplyObject(r);
                    redisFree(nctx);
                    return;
                }
                freeReplyObject(r);
            }
            _redisContexts.push(nctx);
        }
    } else {
        _redisContexts.push(ctx);
    }
    _condition.notify_one();
}

void RedisconPool::close() {
    _b_stop = true;
    _condition.notify_all();
}

bool RedisconPool::checkConnection(redisContext* ctx) {
    if (!ctx || ctx->err) return false;
    auto reply = (redisReply*)redisCommand(ctx, "PING");
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_STATUS || reply->type == REDIS_REPLY_STRING);
    freeReplyObject(reply);
    return ok;
}

bool RedisconPool::isHealthy() const {
    return !_b_stop && !_redisContexts.empty();
}

size_t RedisconPool::getAvailableCount() {
    std::lock_guard<std::mutex> lk(_mutex);
    return _redisContexts.size();
}
