// ChatServer/CSession.cpp
//
// Created by EDPerson on 2026/2/13.
//

#include "CSession.h"
#include "SessionManager.h"
#include "ChatLogicSystem.h"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/post.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <iostream>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

CSession::CSession(boost::asio::io_context& ioc)
    : _ioc(ioc),
      _socket(ioc),
      _authenticated(false),
      _is_sending(false),
      _auth_timer(ioc),
      _idle_timer(ioc) {
    _uuid = boost::uuids::random_generator()();
}

void CSession::Start() {
    auto self = shared_from_this();

    // 10秒认证超时
    _auth_timer.expires_after(std::chrono::seconds(10));
    _auth_timer.async_wait([this, self](boost::system::error_code ec) {
        if (!ec && !_authenticated) {
            std::cerr << "[Auth] session " << boost::uuids::to_string(_uuid)
                      << " authentication timeout, closing" << std::endl;
            boost::system::error_code ignored;
            _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
            _socket.close(ignored);
        }
    });

    ReadNext();
}

void CSession::ReadNext() {
    auto self = shared_from_this();
    boost::asio::async_read(_socket,
        boost::asio::buffer(_header_buffer),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                HandleReadHeader();
            } else {
                std::cout << "Read header error: " << ec.message()
                          << " [uuid: " << boost::uuids::to_string(_uuid) << "]" << std::endl;
                if (!_uid.empty()) {
                    SessionManager::GetInstance()->RemoveSession(_uid, self);
                }
            }
        });
}

void CSession::HandleReadHeader() {
    uint16_t msg_id, msg_len;
    memcpy(&msg_id, &_header_buffer[0], sizeof(uint16_t));
    memcpy(&msg_len, &_header_buffer[2], sizeof(uint16_t));

    msg_id = ntohs(msg_id);
    msg_len = ntohs(msg_len);

    std::cout << "Received msg_id=" << msg_id << ", len=" << msg_len << std::endl;
    HandleReadBody(msg_id, msg_len);
}

void CSession::HandleReadBody(uint16_t msg_id, uint16_t msg_len) {
    auto self = shared_from_this();

    if (msg_len > MAX_MSG_BODY) {
        std::cerr << "[Security] msg_len=" << msg_len
                  << " exceeds MAX_MSG_BODY=" << MAX_MSG_BODY
                  << ", closing connection [uuid: "
                  << boost::uuids::to_string(_uuid) << "]" << std::endl;
        boost::system::error_code ignored;
        _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
        _socket.close(ignored);
        _auth_timer.cancel();
        if (!_uid.empty()) SessionManager::GetInstance()->RemoveSession(_uid, self);
        return;
    }

    _body_buffer.resize(msg_len);

    boost::asio::async_read(_socket,
        boost::asio::buffer(_body_buffer),
        [this, self, msg_id](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                ResetIdleTimer();

                std::string body_str(_body_buffer.begin(), _body_buffer.end());
                std::cout << "Received body: " << body_str << std::endl;

                ChatLogicSystem::GetInstance()->HandleMessage(msg_id, shared_from_this(), body_str);
                ReadNext();
            } else {
                std::cout << "Read body error: " << ec.message() << std::endl;
                if (!_uid.empty()) {
                    SessionManager::GetInstance()->RemoveSession(_uid, self);
                }
            }
        });
}

void CSession::Send(int msg_id, const std::string& json_data) {
    if (json_data.size() > MAX_MSG_BODY) {
        std::cerr << "[Send] payload size=" << json_data.size()
                  << " exceeds MAX_MSG_BODY=" << MAX_MSG_BODY
                  << ", msg_id=" << msg_id << " dropped" << std::endl;
        return;
    }

    std::vector<char> message;
    uint16_t net_id  = htons(static_cast<uint16_t>(msg_id));
    uint16_t net_len = htons(static_cast<uint16_t>(json_data.size()));
    message.insert(message.end(), (char*)&net_id,  (char*)&net_id  + 2);
    message.insert(message.end(), (char*)&net_len, (char*)&net_len + 2);
    message.insert(message.end(), json_data.begin(), json_data.end());

    boost::asio::post(_ioc,
        [this, self = shared_from_this(), msg = std::move(message)]() mutable {
            _send_queue.push(std::move(msg));
            if (!_is_sending) HandleWrite();
        });
}

void CSession::HandleWrite() {
    auto self = shared_from_this();
    _is_sending = true;

    auto& msg = _send_queue.front();
    boost::asio::async_write(_socket,
        boost::asio::buffer(msg),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                _send_queue.pop();
                if (_send_queue.empty()) {
                    _is_sending = false;
                } else {
                    HandleWrite();
                }
            } else {
                std::cout << "Write error: " << ec.message() << std::endl;
                _is_sending = false;
                if (!_uid.empty()) {
                    SessionManager::GetInstance()->RemoveSession(_uid, self);
                }
            }
        });
}

void CSession::ResetIdleTimer() {
    auto self = shared_from_this();
    _idle_timer.expires_after(std::chrono::seconds(kIdleTimeoutSec));

    _idle_timer.async_wait([this, self](boost::system::error_code ec) {
        if (ec == boost::asio::error::operation_aborted) return;

        if (!ec) {
            if (_idle_timer.expiry() <= std::chrono::steady_clock::now()) {
                std::cerr << "[Heartbeat] session " << boost::uuids::to_string(_uuid)
                          << " uid=" << _uid
                          << " idle timeout (" << kIdleTimeoutSec << "s), closing" << std::endl;
                boost::system::error_code ignored;
                _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
                _socket.close(ignored);
                _auth_timer.cancel();
                if (!_uid.empty()) {
                    SessionManager::GetInstance()->RemoveSession(_uid, self);
                }
            }
        }
    });
}

void CSession::SetAuthenticated(const std::string& uid, const std::string& user) {
    _uid = uid;
    _user = user;
    _authenticated = true;
    _auth_timer.cancel();
}

boost::asio::ip::tcp::socket& CSession::GetSocket() {
    return _socket;
}

boost::uuids::uuid CSession::GetUuid() {
    return _uuid;
}

void CSession::Close() {
    boost::system::error_code ignored_ec;
    _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    _socket.close(ignored_ec);

    _idle_timer.cancel();
    _auth_timer.cancel();
}