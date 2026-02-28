//
// Created by EDPerson on 2026/2/3.
//

#ifndef WXCHAT_HTTPCONNECTION_H
#define WXCHAT_HTTPCONNECTION_H
#include "const.h"
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

class HttpConnection:public std::enable_shared_from_this<HttpConnection> {
    friend class CServer;  // 让CServer可以访问_socket
public:
    HttpConnection(boost::asio::io_context& ioc);
    void start();
    http::response<http::dynamic_body> _response;
    http::request<http::dynamic_body> _request;
    std::string _get_url;
    std:: unordered_map<std::string, std::string> _get_params;

private:
    unsigned char ToHex(unsigned char x);
    unsigned char FromHex(unsigned char x);
    std::string UrlEncode(const std::string& str);
    std::string UrlDecode(const std::string& str);
    void PreParseGetParam();
    void Cheakdeadline();
    void HandleRequest();
    void WriteResponse();
    tcp::socket _socket;
    beast::flat_buffer _buf{8192};
    boost::asio::io_context & _ioc;
    net::steady_timer _deadline{_socket.get_executor(),std::chrono::seconds(60)};

};


#endif //WXCHAT_HTTPCONNECTION_H