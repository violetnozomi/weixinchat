#include "RedisconPool.h"
#include <iostream>
#include <chrono>

RedisconPool::RedisconPool(std::size_t poolSize, const char* host, int port, const char* pwd)
    : _poolSize(poolSize), _host(host), _port(port), _pwd(pwd), _b_stop(false)
{
    for (std::size_t i = 0; i < poolSize; ++i) {
        redisContext* ctx = redisConnect(_host.c_str(), _port);
        if (!ctx || ctx->err) {
            if (ctx) redisFree(ctx);
            std::cerr << "[Redis] connect failed (slot " << i << ")" << std::endl;
            continue;
        }
        if (!_pwd.empty()) {
            auto reply = (redisReply*)redisCommand(ctx, "AUTH %s", _pwd.c_str());
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
    // 1. 先发出停机信号，拒绝新的借连请求
    close();

    // 2. 等待所有已借出连接全部归还，最多待 5 秒（防止卡死）
    {
        std::unique_lock<std::mutex> lk(_mutex);
        _all_returned.wait_for(lk, std::chrono::seconds(5),
                               [this]{ return _leased == 0; });
        if (_leased > 0) {
            std::cerr << "[Redis] ~RedisconPool: " << _leased
                      << " connection(s) still in-flight after 5s, force closing" << std::endl;
        }
    }

    // 3. 释放队列里的空闲连接
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
    ++_leased;   // 记录借出数
    return ctx;
}

void RedisconPool::returnContext(redisContext* ctx) {
    std::lock_guard<std::mutex> lk(_mutex);
    --_leased;   // 归还时减计数
    if (_b_stop) {
        redisFree(ctx);
        // 如果所有连接已归还，唤醒析构函数的等待
        if (_leased == 0) _all_returned.notify_all();
        return;
    }
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
    {
        std::lock_guard<std::mutex> lk(_mutex);  // 加锁写，消除与 getRedisContext 的数据竞争
        _b_stop = true;
    }
    _condition.notify_all();   // 唤醒所有在 getRedisContext 里等待的线程
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
