#include "CServer.h"
#include "IOservicePool.h"
#include "ChatLogicSystem.h"
#include "SessionManager.h"
#include "StatusGrpcClient.h"
#include "ChatGrpcClient.h"
#include "ChatServiceImpl.h"
#include "ConfigMgr.h"
#include "AgentManager.h"
#include "DbWriteQueue.h"
#include "DbReadQueue.h"
#include "MySqlPool.h"
#include "RedisMgr.h"
#include "SnowflakeID.h"
#include <iostream>
#include <thread>
#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#ifdef _WIN32
#include <windows.h>
#endif

// 在独立线程中运行 ChatServer 的 gRPC 服务（供其他 ChatServer 调用）
static std::unique_ptr<grpc::Server> g_grpc_server;

void RunGrpcServer() {
    auto& cfg = ConfigMgr::Inst();
    std::string grpc_port = cfg["ChatServer"]["GrpcPort"];
    std::string listen_addr = "0.0.0.0:" + grpc_port;

    ChatServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    g_grpc_server = builder.BuildAndStart();
    std::cout << "ChatServer gRPC listening on " << listen_addr << std::endl;
    g_grpc_server->Wait();  // 阻塞直到服务器关闭
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
        // 连接数 = 逻辑线程(hw) + DbReadQueue workers(≤4) + DbWriteQueue workers(≤3) + 余量
        // 使用 hardware_concurrency + 8 作为动态公式，最少 12 条
        const int mysql_pool_size = std::max(12,
            static_cast<int>(std::thread::hardware_concurrency()) + 8);
        MysqlConPool::GetInstance()->Init(mysql_pool_size);
        // 初始化雪花 ID 生成器（节点 ID 从 config.ini [Snowflake] MachineID 读取，范围 0-1023）
        {
            int64_t mid = 1;
            try { mid = std::stoll(ConfigMgr::Inst()["Snowflake"]["MachineID"]); } catch (...) {}
            SnowflakeID::GetInstance()->Init(mid);
        }
        RedisMgr::GetInstance();                  // 初始化 Redis 连接池
        ChatLogicSystem::GetInstance();
        DbWriteQueue::GetInstance();              // 启动数据库异步写队列
        DbReadQueue::GetInstance();               // 启动数据库异步读队列
        AgentManager::GetInstance();              // 初始化 AI 机器人（预读配置 + SSL context）
        SessionManager::GetInstance();
        StatusGrpcClient::GetInstance();
        ChatGrpcClient::GetInstance();

        // 在后台线程中启动本机的 gRPC 服务
        std::thread grpc_thread(RunGrpcServer);

        // 创建主线程 io_context
        boost::asio::io_context ioc;

        // 注册信号处理，优雅停机
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](boost::system::error_code /*ec*/, int /*sig*/) {
            ioc.stop();
        });

        unsigned short port = 8081;
        CServer server(ioc, port);

        std::cout << "ChatServer TCP listening on port " << port << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        std::cout << "=================================" << std::endl;

        ioc.run();

        // ioc 停止后优雅关闭 gRPC 服务和逻辑线程
        if (g_grpc_server) g_grpc_server->Shutdown();
        if (grpc_thread.joinable()) grpc_thread.join();

        ChatLogicSystem::GetInstance()->Stop();
        DbWriteQueue::GetInstance()->Stop();
        DbReadQueue::GetInstance()->Stop();

    } catch (std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "ChatServer stopped" << std::endl;
    return 0;
}