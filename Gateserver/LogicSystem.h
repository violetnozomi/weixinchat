//
// Created by EDPerson on 2026/2/3.
//

#ifndef WXCHAT_LOGICSYSTEM_H
#define WXCHAT_LOGICSYSTEM_H
#include "Singleton.h"
#include "const.h"
#include <functional>
#include <map>

class HttpConnection;
typedef std::function<void(std::shared_ptr<HttpConnection>)> HttpHandler;
class LogicSystem:public Singleton<LogicSystem>{
    friend class Singleton<LogicSystem>;
    public:
    ~LogicSystem(){};
    bool HandleGet(std::string ,std::shared_ptr<HttpConnection>);
    bool HandlePost(std::string ,std::shared_ptr<HttpConnection>);
    void RegGet(std::string url, HttpHandler);
    void RegPost(std::string url, HttpHandler);
private:
    LogicSystem();
    std::map<std:: string,HttpHandler> _get_handler;
    std::map<std:: string,HttpHandler> _post_handler;
};


#endif //WXCHAT_LOGICSYSTEM_H