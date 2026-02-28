// ChatServer/SessionManager.cpp
//
// Created by EDPerson on 2026/2/13.
//

#include "SessionManager.h"
#include "CSession.h"
#include "RedisMgr.h"
#include <iostream>

void SessionManager::AddSession(const std::string& uid,
                                std::shared_ptr<CSession> session) {
    std::lock_guard<std::mutex> lock(_mutex);

    // 如果用户已经在线，关闭旧连接 socket，防止僵尸 session 占资源
    auto it = _uid_to_session.find(uid);
    if (it != _uid_to_session.end()) {
        std::cout << "User already online, closing old session before replacing: uid=" << uid << std::endl;
        boost::system::error_code ignored;
        it->second->GetSocket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
        it->second->GetSocket().close(ignored);
    }

    _uid_to_session[uid] = session;
    std::cout << "Session added: uid=" << uid
              << ", total online=" << _uid_to_session.size() << std::endl;
}

void SessionManager::RemoveSession(const std::string& uid, const std::shared_ptr<CSession>& session) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _uid_to_session.find(uid);
    if (it == _uid_to_session.end()) return;

    // 关键：只允许“自己删自己”，防止旧连接误删新连接
    if (it->second != session) {
        std::cout << "Skip stale session remove: uid=" << uid << std::endl;
        return;
    }

    _uid_to_session.erase(it);
    std::cout << "Session removed: uid=" << uid
              << ", total online=" << _uid_to_session.size() << std::endl;

    // 清 Redis 路由
    RedisMgr::GetInstance()->Del("uid_server:" + uid);
}

std::shared_ptr<CSession> SessionManager::GetSession(const std::string& uid) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _uid_to_session.find(uid);
    return (it != _uid_to_session.end()) ? it->second : nullptr;
}

bool SessionManager::IsUserOnline(const std::string& uid) {
    std::lock_guard<std::mutex> lock(_mutex);
    return _uid_to_session.find(uid) != _uid_to_session.end();
}

size_t SessionManager::GetOnlineCount() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _uid_to_session.size();
}