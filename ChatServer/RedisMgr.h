#pragma once
#include <hiredis/hiredis.h>
#include <memory>
#include <string>
#include <atomic>
#include "Singleton.h"
#include "RedisconPool.h"

class RedisMgr : public Singleton<RedisMgr>,
                 public std::enable_shared_from_this<RedisMgr>
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
    void Close();

private:
    RedisMgr();
    std::unique_ptr<RedisconPool> _pool;
};
