//
// Created by EDPerson on 2026/2/13.
//

#ifndef CHATSERVER_CHATLOGICSYSTEM_H
#define CHATSERVER_CHATLOGICSYSTEM_H

#include "const.h"
#include "Singleton.h"
#include <functional>
#include <map>
#include <string>

class CSession;

class ChatLogicSystem : public Singleton<ChatLogicSystem> {
    friend class Singleton<ChatLogicSystem>;
    
public:
    using MsgHandler = std::function<void(
        std::shared_ptr<CSession>, 
        int msg_id, 
        const std::string&)>;
    
    void RegisterHandler(int msg_id, MsgHandler handler);
    bool HandleMessage(int msg_id, 
                      std::shared_ptr<CSession> session,
                      const std::string& json_str);
    
private:
    ChatLogicSystem();
    std::map<int, MsgHandler> _handlers;
};

#endif //CHATSERVER_CHATLOGICSYSTEM_H
