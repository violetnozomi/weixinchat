// ChatServer/SessionManager.h
//
// Created by EDPerson on 2026/2/13.
//

#ifndef CHATSERVER_SESSIONMANAGER_H
#define CHATSERVER_SESSIONMANAGER_H

#include "const.h"
#include "Singleton.h"
#include <unordered_map>
#include <mutex>
#include <memory>

class CSession;

class SessionManager : public Singleton<SessionManager> {
    friend class Singleton<SessionManager>;

public:
    // 添加会话
    void AddSession(const std::string& uid, std::shared_ptr<CSession> session);

    // 移除会话（仅当当前映射仍是该 session 时才删除，防止旧连接误删新连接）
    void RemoveSession(const std::string& uid, const std::shared_ptr<CSession>& session);

    // 获取会话
    std::shared_ptr<CSession> GetSession(const std::string& uid);

    // 检查用户是否在线
    bool IsUserOnline(const std::string& uid);

    // 获取在线用户数
    size_t GetOnlineCount();

private:
    SessionManager() = default;
    std::mutex _mutex;
    std::unordered_map<std::string, std::shared_ptr<CSession>> _uid_to_session;
};

#endif //CHATSERVER_SESSIONMANAGER_H