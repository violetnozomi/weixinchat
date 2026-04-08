//
// Created by EDPerson on 2026/2/8.
//

#include "RedisMgr.h"
#include "RedisconPool.h"
#include "ConfigMgr.h"
#include <string>

// RAII连接管理类实现
RedisConnectionGuard::RedisConnectionGuard(RedisconPool* pool) 
    : _pool(pool), _context(nullptr) {
    if (_pool) {
        _context = _pool->getRedisContext();
    }
}

RedisConnectionGuard::~RedisConnectionGuard() {
    if (_pool && _context) {
        _pool->returnContext(_context);
    }
}

RedisMgr::RedisMgr() {
    auto& gCfgMgr = ConfigMgr::Inst();
    auto hoststr = gCfgMgr["Redis"]["Host"];
    auto portstr = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Passwd"];
    _con_pool.reset(new RedisconPool(5, hoststr.c_str(), std::stoi(portstr), pwd.c_str()));
    
    if (!_con_pool->isHealthy()) {
        std::cerr << "警告: Redis连接池初始化可能失败，可用连接数: " 
                  << _con_pool->getAvailableCount() << std::endl;
    }
}

RedisMgr::~RedisMgr() {
    Close();
}



bool RedisMgr::Get(const std::string &key, std::string &value) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "GET %s", key.c_str());
    if (reply == nullptr) {
        return false;
    }
    
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        return false;
    }
    
    if (reply->type != REDIS_REPLY_STRING) {
        freeReplyObject(reply);
        return false;
    }
    
    if (reply->str != nullptr) {
        value = std::string(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::Set(const std::string &key, const std::string &value) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ SET  " << key << " << " << value <<" ] failed: 无法获取连接" << std::endl;
        return false;
    }
    
    // 使用二进制安全的 redisCommandArgv，避免值含空格时协议错误
    const char* argv[] = {"SET", key.c_str(), value.c_str()};
    size_t argvlen[] = {3, key.length(), value.length()};
    auto reply = (redisReply*)redisCommandArgv(guard.getContext(), 3, argv, argvlen);
    if (reply == nullptr) {
        std::cout << "[ SET  " << key << " << " << value <<" ] failed: reply为NULL" << std::endl;
        return false;
    }
    
    if (!(reply->type == REDIS_REPLY_STATUS && (strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0))) {
        std::cout << "[ SET  " << key << " << " << value <<" ] failed" << std::endl;
        freeReplyObject(reply);
        return false;
    }
    
    std::cout << "[ SET  " << key << " ] success" << std::endl;
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::Auth(const std::string &password) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ AUTH ] failed: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "AUTH %s", password.c_str());
    if (reply == nullptr) {
        return false;
    }
    
    if (reply->type == REDIS_REPLY_ERROR) {
        freeReplyObject(reply);
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::LPush(const std::string &key, const std::string &value) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ LPUSH  " << key << " " << value <<" ] failed: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "LPUSH %s %s", key.c_str(), value.c_str());
    if (reply == nullptr) {
        std::cout << "[ LPUSH  " << key << " " << value <<" ] failed: reply为NULL" << std::endl;
        return false;
    }
    
    if (reply->type == REDIS_REPLY_ERROR) {
        std::cout << "[ LPUSH  " << key << " " << value <<" ] failed" << std::endl;
        freeReplyObject(reply);
        return false;
    }
    
    std::cout << "[ LPUSH  " << key << " " << value <<" ] success" << std::endl;
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::LPop(const std::string &key, std::string& value){
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ LPOP " << key << " ] failure: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "LPOP %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "[ LPOP " << key << " ] failure: reply为NULL" << std::endl;
        return false;
    }
    
    if (reply->type == REDIS_REPLY_NIL) {
        std::cout << "[ LPOP " << key << " ] failure: key不存在或列表为空" << std::endl;
        freeReplyObject(reply);
        return false;
    }
    
    value = reply->str;
    std::cout << "[ LPOP " << key << " ] success" << std::endl;
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::RPush(const std::string& key, const std::string& value) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ RPUSH " << key << "  " << value << " ] failure: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "RPUSH %s %s", key.c_str(), value.c_str());
    if (reply == nullptr) {
        std::cout << "[ RPUSH " << key << "  " << value << " ] failure: reply为NULL" << std::endl;
        return false;
    }

    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        std::cout << "[ RPUSH " << key << "  " << value << " ] failure: 返回错误" << std::endl;
        freeReplyObject(reply);
        return false;
    }

    std::cout << "[ RPUSH " << key << "  " << value << " ] success" << std::endl;
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::RPop(const std::string& key, std::string& value) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ RPOP " << key << " ] failure: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "RPOP %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "[ RPOP " << key << " ] failure: reply为NULL" << std::endl;
        return false;
    }
    
    if (reply->type == REDIS_REPLY_NIL) {
        std::cout << "[ RPOP " << key << " ] failure: key不存在或列表为空" << std::endl;
        freeReplyObject(reply);
        return false;
    }
    
    value = reply->str;
    std::cout << "[ RPOP " << key << " ] success" << std::endl;
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::HSet(const std::string &key, const std::string &hkey, const std::string &value) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ HSet " << key << "  " << hkey <<"  " << value << " ] failure: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "HSET %s %s %s", key.c_str(), hkey.c_str(), value.c_str());
    if (reply == nullptr) {
        std::cout << "[ HSet " << key << "  " << hkey <<"  " << value << " ] failure: reply为NULL" << std::endl;
        return false;
    }
    
    if (reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "[ HSet " << key << "  " << hkey <<"  " << value << " ] failure" << std::endl;
        freeReplyObject(reply);
        return false;
    }
    
    std::cout << "[ HSet " << key << "  " << hkey << "  " << value << " ] success" << std::endl;
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen)
{
    const char* argv[4];
    size_t argvlen[4];
    argv[0] = "HSET";
    argvlen[0] = 4;
    argv[1] = key;
    argvlen[1] = strlen(key);
    argv[2] = hkey;
    argvlen[2] = strlen(hkey);
    argv[3] = hvalue;
    argvlen[3] = hvaluelen;
    
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ HSet " << key << "  " << hkey << " ] failure: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommandArgv(guard.getContext(), 4, argv, argvlen);
    if (reply == nullptr) {
        std::cout << "[ HSet " << key << "  " << hkey << " ] failure: reply为NULL" << std::endl;
        return false;
    }
    
    if (reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "[ HSet " << key << "  " << hkey << " ] failure" << std::endl;
        freeReplyObject(reply);
        return false;
    }
    
    std::cout << "[ HSet " << key << "  " << hkey << " ] success" << std::endl;
    freeReplyObject(reply);
    return true;
}
std::string RedisMgr::HGet(const std::string &key, const std::string &hkey)
{
    const char* argv[3];
    size_t argvlen[3];
    argv[0] = "HGET";
    argvlen[0] = 4;
    argv[1] = key.c_str();
    argvlen[1] = key.length();
    argv[2] = hkey.c_str();
    argvlen[2] = hkey.length();
    
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ HGet " << key << " "<< hkey <<" ] failure: 无法获取连接" << std::endl;
        return "";
    }
    
    auto reply = (redisReply*)redisCommandArgv(guard.getContext(), 3, argv, argvlen);
    if (reply == nullptr) {
        std::cout << "[ HGet " << key << " "<< hkey <<" ] failure: reply为NULL" << std::endl;
        return "";
    }
    
    if (reply->type == REDIS_REPLY_NIL) {
        std::cout << "[ HGet " << key << " "<< hkey <<" ] failure: field不存在" << std::endl;
        freeReplyObject(reply);
        return "";
    }

    std::string value = reply->str;
    freeReplyObject(reply);
    std::cout << "[ HGet " << key << " " << hkey << " ] success" << std::endl;
    return value;
}
bool RedisMgr::Del(const std::string &key) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ DEL " << key << " ] failure: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "DEL %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "[ DEL " << key << " ] failure: reply为NULL" << std::endl;
        return false;
    }
    
    if (reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "[ DEL " << key << " ] failure" << std::endl;
        freeReplyObject(reply);
        return false;
    }
    
    std::cout << "[ DEL " << key << " ] success" << std::endl;
    freeReplyObject(reply);
    return true;
}
bool RedisMgr::ExistsKey(const std::string &key) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        std::cout << "[ EXISTS " << key << " ] failure: 无法获取连接" << std::endl;
        return false;
    }
    
    auto reply = (redisReply*)redisCommand(guard.getContext(), "EXISTS %s", key.c_str());
    if (reply == nullptr) {
        std::cout << "[ EXISTS " << key << " ] failure: reply为NULL" << std::endl;
        return false;
    }
    
    if (reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "[ EXISTS " << key << " ] failure" << std::endl;
        freeReplyObject(reply);
        return false;
    }
    
    bool exists = (reply->integer == 1);
    freeReplyObject(reply);
    std::cout << "[ EXISTS " << key << " ] " << (exists ? "存在" : "不存在") << std::endl;
    return exists;
}
void RedisMgr::Close() {
    if (_con_pool) {
        _con_pool->close();
    }
}

bool RedisMgr::Expire(const std::string &key, int seconds) {
    RedisConnectionGuard guard(_con_pool.get());
    if (!guard.isValid()) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(guard.getContext(), "EXPIRE %s %d", key.c_str(), seconds);
    if (reply == nullptr) {
        return false;
    }
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}


