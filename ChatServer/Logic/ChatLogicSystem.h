//
// Created by EDPerson on 2026/2/13.
//

#ifndef CHATSERVER_CHATLOGICSYSTEM_H
#define CHATSERVER_CHATLOGICSYSTEM_H

#include "const.h"
#include "Singleton.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/thread_pool.hpp>

#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <string>

class CSession;

struct TextRouteResult {
    bool delivered_live = false;
    bool queued_offline = false;
    int error = ErrorCodes::Success;
};

class ChatLogicSystem : public Singleton<ChatLogicSystem> {
    friend class Singleton<ChatLogicSystem>;

public:
    using MsgHandler = std::function<void(
        std::shared_ptr<CSession>,
        int msg_id,
        const std::string&)>;

    void RegisterHandler(int msg_id, MsgHandler handler);
    boost::asio::any_io_executor GetLogicExecutor();
    void DispatchMessage(std::shared_ptr<CSession> session, int msg_id, const std::string& body);
    void PostTask(std::weak_ptr<CSession> routing, std::function<void()> task);
    void PostMessage(int msg_id, std::shared_ptr<CSession> session, const std::string& body);
    TextRouteResult RouteTextMessage(int from_uid,
                                     int to_uid,
                                     const std::string& content,
                                     std::time_t timestamp = 0);
    void Stop();

    ~ChatLogicSystem();

private:
    ChatLogicSystem();

    std::map<int, MsgHandler> _handlers;
    boost::asio::thread_pool _logic_pool;
};

#endif // CHATSERVER_CHATLOGICSYSTEM_H
