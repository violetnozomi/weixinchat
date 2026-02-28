//
// Created by EDPerson on 2026/2/3.
//

#ifndef WXCHAT_CSERVER_H
#define WXCHAT_CSERVER_H

#include "const.h"
#include "HttpConnection.h"
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

class CServer:public std::enable_shared_from_this<CServer> {
public:
    CServer(net::io_context& ioc , unsigned short port );
    void start();
    private:
    net::io_context &_ioc;
    tcp::acceptor _acceptor;
    // 移除_socket成员，每次accept时动态创建
};


#endif //WXCHAT_CSERVER_H