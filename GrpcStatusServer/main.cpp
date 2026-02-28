
#include "StatusServiceImpl.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "const.h"
#include "ConfigMgr.h"
#include "hiredis/hiredis.h"
#include "RedisMgr.h"
#include "MySqlPool.h"
#include "IOServicePool.h"
#include <boost/asio.hpp>

// ... 后面的代码保持不变 ...
void ServerRun() {
    auto & cfg = ConfigMgr::Inst();
    auto server_add = (cfg["StatusServer"]["Host"]+":"+ cfg["StatusServer"]["Port"]);
    StatusServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_add, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_add << std::endl;
    boost::asio::io_context ioc;
    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&server](boost::system::error_code ec, int signal_num) {
        if (!ec) {
            std::cout << "Shutting down server..." << std::endl;
           server->Shutdown(); // 优雅地关闭服务器
        }
    });
    std::thread([&ioc]() {
           ioc.run();
       }).detach();
    server->Wait();
    ioc.stop();
}
int main() {
    try {
        // 初始化MySQL连接池
        std::cout << "Initializing MySQL connection pool..." << std::endl;
        MysqlConPool::GetInstance()->Init(5);
        std::cout << "MySQL connection pool initialized" << std::endl;
        
        ServerRun();
    }catch (std::exception & e) {
        std::cout << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;

}