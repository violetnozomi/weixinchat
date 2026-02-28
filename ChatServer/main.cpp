#include "CServer.h"
#include "IOservicePool.h"
#include "ChatLogicSystem.h"
#include "SessionManager.h"
#include "StatusGrpcClient.h"
#include "ChatGrpcClient.h"
#include "ChatServiceImpl.h"
#include "ConfigMgr.h"
#include "MySqlPool.h"
#include "RedisMgr.h"
#include <iostream>
#include <thread>
#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#ifdef _WIN32
#include <windows.h>
#endif

// 在独立线程中运行 ChatServer 的 gRPC 服务（供其他 ChatServer 调用）
void RunGrpcServer() {
    auto& cfg = ConfigMgr::Inst();
    std::string grpc_port = cfg["ChatServer"]["GrpcPort"];
    std::string listen_addr = "0.0.0.0:" + grpc_port;

    ChatServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto grpc_server = builder.BuildAndStart();
    std::cout << "ChatServer gRPC listening on " << listen_addr << std::endl;
    grpc_server->Wait();  // 阻塞直到服务器关闭
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    try {
        std::cout << "=================================" << std::endl;
        std::cout << "  ChatServer Starting..." << std::endl;
        std::cout << "=================================" << std::endl;

        // 初始化单例
        AsioioservicePool::GetInstance();
        MysqlConPool::GetInstance()->Init(5);     // 初始化 MySQL 连接池（5个连接）
        RedisMgr::GetInstance();                  // 初始化 Redis 连接池
        ChatLogicSystem::GetInstance();
        SessionManager::GetInstance();
        StatusGrpcClient::GetInstance();
        ChatGrpcClient::GetInstance();

        // 在后台线程中启动本机的 gRPC 服务
        std::thread grpc_thread(RunGrpcServer);
        grpc_thread.detach();

        // 创建主线程 io_context
        boost::asio::io_context ioc;

        unsigned short port = 8081;
        CServer server(ioc, port);

        std::cout << "ChatServer TCP listening on port " << port << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        std::cout << "=================================" << std::endl;

        ioc.run();

    } catch (std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "ChatServer stopped" << std::endl;
    return 0;
}