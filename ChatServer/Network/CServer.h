//
// Created by EDPerson on 2026/2/13.
//

#ifndef CHATSERVER_CSERVER_H
#define CHATSERVER_CSERVER_H
#include "CSession.h"
#include "const.h"
#include "boost/uuid.hpp"
#include "boost/uuid//uuid_generators.hpp"
#include "boost/asio.hpp"
#include <boost/functional/hash.hpp>
class CServer {
public:
    CServer(boost::asio::io_context& ioc , short port);
    ~CServer();
private:
    void Start();
    void PurgeSessions();   // 清除已断开的弱引用条目
    // weak_ptr 确保不延长 session 生命周期，连接断开后条目自动过期
    std::unordered_map<boost::uuids::uuid, std::weak_ptr<CSession>, boost::hash<boost::uuids::uuid>> _sessions;
    boost::asio::io_context& _ioc;
    short _port;
    boost::asio::ip::tcp::acceptor _acceptor;
    std::mutex _mutex;
};


#endif //CHATSERVER_CSERVER_H