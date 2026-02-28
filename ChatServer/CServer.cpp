//
// Created by EDPerson on 2026/2/13.
//

#include "CServer.h"
#include "IOservicePool.h"
#include <iostream>
#include <boost/uuid/uuid_io.hpp>

CServer::CServer(boost::asio::io_context &ioc, short port)
    : _ioc(ioc), _port(port), 
      _acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    std::cout << "ChatServer initialized on port " << port << std::endl;
    Start();
}

CServer::~CServer() {
    std::cout << "ChatServer shutdown" << std::endl;
}

void CServer::Start() {
    auto& io_context = AsioioservicePool::GetInstance()->Get_iocontext();
    auto new_session = std::make_shared<CSession>(io_context);
    
    _acceptor.async_accept(new_session->GetSocket(),
        [this, new_session](boost::system::error_code ec) {
            if (!ec) {
                std::cout << "Accepted new connection: " 
                          << boost::uuids::to_string(new_session->GetUuid()) << std::endl;
                new_session->Start();
                
                std::lock_guard<std::mutex> lock(_mutex);
                _sessions[new_session->GetUuid()] = new_session;  // weak_ptr 隐式转换
                PurgeSessions();   // 顺便清理已断开的过期条目
            } else {
                std::cout << "Accept failed: " << ec.message() << std::endl;
            }
            
            // 继续接受下一个连接
            Start();
        });
}

void CServer::PurgeSessions() {
    // 调用方已持有 _mutex，直接遍历清除过期 weak_ptr
    for (auto it = _sessions.begin(); it != _sessions.end(); ) {
        if (it->second.expired()) {
            it = _sessions.erase(it);
        } else {
            ++it;
        }
    }
}
