//
// Created by EDPerson on 2026/2/8.
//

#ifndef WXCHAT_REDISCONPOOL_H
#define WXCHAT_REDISCONPOOL_H
#include <cwchar>
#include "const.h"
#include <hiredis/hiredis.h>


class RedisconPool {
public:
    RedisconPool(std::size_t poolSize,const char* host,int port, const char* pwd);
    ~RedisconPool();
    redisContext* getRedisContext();
    void returnContext(redisContext* context);
    void close();
    bool isHealthy() const;
    size_t getPoolSize() const { return _poolSize; }
    size_t getAvailableCount();
private:
    bool checkConnection(redisContext* context);
    std::queue<redisContext*> _redisContexts;
    std::mutex _mutex;
    size_t _poolSize;
    std::string _host;
    int   _port;
    std::string  _pwd;
    std::condition_variable _condition;
    bool _b_stop;


};


#endif //WXCHAT_REDISCONPOOL_H