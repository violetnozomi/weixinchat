//
// Created by EDPerson on 2026/2/13.
//

#ifndef CHATSERVER_CSESSION_H
#define CHATSERVER_CSESSION_H
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/uuid/uuid.hpp>
#include <array>
#include <vector>
#include <queue>

#include "const.h"

class CSession:public std::enable_shared_from_this<CSession>{
    // 消息体最大长度（32 KB），防止恶意超大包耗尽内存
    static constexpr uint16_t MAX_MSG_BODY = 32768;

public:
    boost::asio::ip::tcp::socket& GetSocket();
    CSession(boost::asio::io_context&);
    void Start();
    boost::uuids::uuid GetUuid();
    
    // 消息发送
    void Send(int msg_id, const std::string& json_data);
    
    // 认证相关
    std::string GetUid() const { return _uid; }
    std::string GetUser() const { return _user; }
    bool IsAuthenticated() const { return _authenticated; }
    void SetAuthenticated(const std::string& uid, const std::string& user);
    void Close() ;


private:
    void HandleReadHeader();
    void HandleReadBody(uint16_t msg_id, uint16_t msg_len);
    void HandleWrite();
    void ReadNext();      // 开始/继续读取下一条消息头
    
    // Socket和基本信息
    boost::asio::ip::tcp::socket _socket;
    boost::uuids::uuid _uuid;
    boost::asio::io_context& _ioc;
    
    // 消息解析
    std::array<char, 4> _header_buffer;      // [id:2字节][len:2字节]
    std::vector<char> _body_buffer;
    
    // 用户信息
    std::string _uid;
    std::string _user;
    bool _authenticated;
    
    // 发送队列（只在 io_context 线程访问，无需加锁）
    std::queue<std::vector<char>> _send_queue;
    bool _is_sending;
    // 已移除 _send_mutex：所有队列操作通过 boost::asio::post 投递到 io_context 线程，天然串行

    // 认证超时定时器：10 秒内未完成身份验证则踢下线
    boost::asio::steady_timer _auth_timer;

    // 空闲超时定时器：收到任意消息后重置；超时则断开死连接
    static constexpr int kIdleTimeoutSec = 90;   // 3× 客户端心跳间隔
    boost::asio::steady_timer _idle_timer;
    void ResetIdleTimer();

};


#endif //CHATSERVER_CSESSION_H