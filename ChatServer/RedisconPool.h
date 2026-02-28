#pragma once
#include "const.h"
#include <hiredis/hiredis.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

class RedisconPool {
public:
    RedisconPool(std::size_t poolSize, const char* host, int port, const char* pwd);
    ~RedisconPool();
    redisContext* getRedisContext();
    void returnContext(redisContext* context);
    void close();
    bool isHealthy() const;
    size_t getAvailableCount();
private:
    bool checkConnection(redisContext* context);
    std::queue<redisContext*> _redisContexts;
    std::mutex _mutex;
    std::condition_variable _condition;
    size_t      _poolSize;
    std::string _host;
    int         _port;
    std::string _pwd;
    bool        _b_stop;
};
