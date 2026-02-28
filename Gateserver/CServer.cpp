//
// Created by EDPerson on 2026/2/3.
//
#include "CServer.h"

#include <iostream>
#include "IoServicePool.h"

CServer::CServer(net::io_context &ioc, unsigned short port)
    :_ioc(ioc),_acceptor(ioc,tcp::endpoint(tcp::v4(),port)){
    std::cout << "服务器监听端口: " << port << std::endl;
}

void CServer::start() {
    auto self = shared_from_this();
    
    // 方案A：从连接池获取worker io_context，让HttpConnection在其上创建socket
    auto& worker_ioc = AsioioservicePool::GetInstance()->Get_iocontext();
    
    // 创建HttpConnection（内部会在worker_ioc上创建socket）
    auto new_conn = std::make_shared<HttpConnection>(worker_ioc);
    
    // Accept到HttpConnection的socket上
    _acceptor.async_accept(new_conn->_socket, [self, new_conn](beast::error_code ec) {
        try {
            if (!ec) {
                std::cout << "接受新连接: " << new_conn->_socket.remote_endpoint() << std::endl;
                
                // socket已经在worker_ioc上运行
                new_conn->start();
            } else {
                std::cerr << "Accept error: " << ec.message() << std::endl;
            }
            
            // 继续接收下一个连接
            self->start();
        } catch (std::exception &e) {
            std::cerr << "Exception in start: " << e.what() << std::endl;
            self->start();
        }
    });
}