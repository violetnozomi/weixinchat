//
// Created by EDPerson on 2026/2/8.
//

#ifndef WXCHAT_REDISMGR_H
#define WXCHAT_REDISMGR_H
#include "const.h"
#include "Singleton.h"
#include <hiredis/hiredis.h>

class RedisconPool;

// RAII连接管理类
class RedisConnectionGuard {
public:
    RedisConnectionGuard(RedisconPool* pool);
    ~RedisConnectionGuard();
    
    redisContext* getContext() const { return _context; }
    bool isValid() const { return _context != nullptr; }
    
    // 禁止拷贝
    RedisConnectionGuard(const RedisConnectionGuard&) = delete;
    RedisConnectionGuard& operator=(const RedisConnectionGuard&) = delete;
    
private:
    RedisconPool* _pool;
    redisContext* _context;
};

class RedisMgr:public Singleton<RedisMgr>,public std::enable_shared_from_this<RedisMgr>  {
    friend class Singleton<RedisMgr>;
public:
    ~RedisMgr();
    bool Get(const std::string &key, std::string& value);
    bool Set(const std::string &key, const std::string &value);
    bool Auth(const std::string &password);
    bool LPush(const std::string &key, const std::string &value);
    bool LPop(const std::string &key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);
    bool HSet(const std::string &key, const std::string  &hkey, const std::string &value);
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
    std::string HGet(const std::string &key, const std::string &hkey);
    bool Del(const std::string &key);
    bool ExistsKey(const std::string &key);
    bool Expire(const std::string &key, int seconds);
    void Close();
private:
    RedisMgr();

    std::unique_ptr<RedisconPool> _con_pool;
};


#endif //WXCHAT_REDISMGR_H