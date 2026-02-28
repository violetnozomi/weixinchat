#include "const.h"
#include "CServer.h"
#include "MySqlPool.h"
#include "RedisMgr.h"

#ifdef _WIN32
#include <windows.h>
#endif

// TIP 要<b>Run</b>代码，请按 <shortcut actionId="Run"/> 或点击装订区域中的 <icon src="AllIcons.Actions.Execute"/> 图标。
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    
    try {
        // 初始化MySQL连接池
        MysqlConPool::GetInstance()->Init(5);
        
        // 初始化Redis连接池
        RedisMgr::GetInstance();
        
        boost::asio::io_context ioc;
        ConfigMgr gCfgMgr;
        std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
        unsigned short port = atoi(gate_port_str.c_str());
        
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](boost::system::error_code const& err,int signumber) {
            if (err) {
                return;
            }
            ioc.stop();
        });
        
        std::make_shared<CServer>(ioc, port)->start();
        std::cout << "服务器启动成功，监听端口: " << port << std::endl;
        ioc.run();
    }catch (std::exception& e) {
        std::cerr << "服务器异常: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}