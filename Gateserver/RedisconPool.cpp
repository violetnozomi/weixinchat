//
// Created by EDPerson on 2026/2/8.
//

#include "RedisconPool.h"
RedisconPool::RedisconPool(std::size_t poolSize,  const char* host, int port, const char *pwd):_host(host),_port(port),_poolSize(poolSize),_pwd(pwd),_b_stop(false) {
    for (std::size_t i = 0; i < poolSize; i++) {
        redisContext* context = redisConnect(_host.data(), _port);
        if (context == NULL||context->err != 0) {
            if (context!=NULL){
                redisFree(context);
            }
            continue;
        }
        auto reply = (redisReply*)redisCommand(context,"AUTH %s",_pwd.data());
        if (reply == NULL) {
            redisFree(context);
            freeReplyObject(reply);
            continue;
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            redisFree(context);
            freeReplyObject(reply);
            continue;
        }
        freeReplyObject(reply);
        _redisContexts.push(context);
    }
}
RedisconPool::~RedisconPool() {
    std::lock_guard<std::mutex> lock(_mutex);
    while (!_redisContexts.empty()) {
       auto con = _redisContexts.front();
        redisFree(con);
        _redisContexts.pop();

    }
}
redisContext *RedisconPool::getRedisContext() {
    std::unique_lock<std::mutex> lock(_mutex);
    _condition.wait(lock, [this]() {
        if (_b_stop) {
            return true;
        }
        return !_redisContexts.empty();
    });
    if (_b_stop) {
        return nullptr;
    }
    auto context = _redisContexts.front();
    _redisContexts.pop();
    return context;
}
void RedisconPool::returnContext(redisContext *context) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_b_stop) {
        redisFree(context);
        return;
    }
    
    // 检查连接健康状态，不健康则丢弃并重新创建
    if (!checkConnection(context)) {
        std::cout << "连接不健康，丢弃并重新创建" << std::endl;
        redisFree(context);
        
        // 尝试创建新连接
        redisContext* newContext = redisConnect(_host.c_str(), _port);
        if (newContext != nullptr && newContext->err == 0) {
            auto reply = (redisReply*)redisCommand(newContext, "AUTH %s", _pwd.c_str());
            if (reply != nullptr && reply->type != REDIS_REPLY_ERROR) {
                freeReplyObject(reply);
                _redisContexts.push(newContext);
                _condition.notify_one();
                return;
            }
            if (reply != nullptr) {
                freeReplyObject(reply);
            }
            redisFree(newContext);
        }
        return;
    }
    
    _redisContexts.push(context);
    _condition.notify_one();
}

void RedisconPool::close() {
    _b_stop = true;
    _condition.notify_all();
}

bool RedisconPool::checkConnection(redisContext* context) {
    if (context == nullptr || context->err != 0) {
        return false;
    }
    // 发送PING命令检查连接是否存活
    auto reply = (redisReply*)redisCommand(context, "PING");
    if (reply == nullptr) {
        return false;
    }
    bool isValid = (reply->type == REDIS_REPLY_STATUS || reply->type == REDIS_REPLY_STRING);
    freeReplyObject(reply);
    return isValid;
}

bool RedisconPool::isHealthy() const {
    return !_b_stop && !_redisContexts.empty();
}

size_t RedisconPool::getAvailableCount() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _redisContexts.size();
}
