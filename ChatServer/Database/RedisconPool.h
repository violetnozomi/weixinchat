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
    std::condition_variable _condition;    // 有可用连接时唤醒等待者
    std::condition_variable _all_returned; // 所有借出连接全部归还时唤醒析构函数
    size_t      _poolSize;
    size_t      _leased{0};  // 当前借出（在途）的连接数
    std::string _host;
    int         _port;
    std::string _pwd;
    bool        _b_stop;
};
