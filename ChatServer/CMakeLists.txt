cmake_minimum_required(VERSION 3.20)
project(ChatServer)

set(CMAKE_CXX_STANDARD 17)

# =========================================================
# 1. 修复 LNK2038：强制使用静态运行时 (MT/MTd)
# =========================================================
if (MSVC)
    add_compile_options(/utf-8 /bigobj)
    add_definitions(-D_WIN32_WINNT=0x0A00)

    # 【关键修复】你的 vcpkg 库是 StaticDebug (MTd)，所以这里必须匹配！
    # 之前让你注释掉是因为我不确定你 vcpkg 的版本，现在报错信息确认了必须加这一行。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# =========================================================
# 2. 查找依赖库
# =========================================================
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(hiredis CONFIG REQUIRED)
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)

# 【关键修复】静态链接 MySQL 需要这两个压缩库
find_package(lz4 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# =========================================================
# 3. 定义源文件（按模块分组）
# =========================================================
set(SOURCE_FILES
        main.cpp

        # Network - TCP 网络层
        Network/CServer.cpp
        Network/CServer.h
        Network/CSession.cpp
        Network/CSession.h
        Network/IOservicePool.cpp
        Network/IOservicePool.h
        Network/SessionManager.cpp
        Network/SessionManager.h

        # Grpc - gRPC 客户端/服务端
        Grpc/ChatGrpcClient.cpp
        Grpc/ChatGrpcClient.h
        Grpc/StatusGrpcClient.cpp
        Grpc/StatusGrpcClient.h
        Grpc/ChatServiceImpl.cpp
        Grpc/ChatServiceImpl.h
        Grpc/GrpcStubPool.h

        # Database - 数据库与存储
        Database/MySqlPool.cpp
        Database/MySqlPool.h
        Database/RedisconPool.cpp
        Database/RedisconPool.h
        Database/RedisMgr.cpp
        Database/RedisMgr.h
        Database/DbWriteQueue.cpp
        Database/DbWriteQueue.h
        Database/DbReadQueue.cpp
        Database/DbReadQueue.h
        Database/MinioMgr.cpp
        Database/MinioMgr.h

        # Logic - 业务逻辑
        Logic/ChatLogicSystem.cpp
        Logic/ChatLogicSystem.h
        Logic/SyncService.cpp
        Logic/SyncService.h

        # Utils - 工具与配置
        Utils/ConfigMgr.cpp
        Utils/ConfigMgr.h
        Utils/const.h
        Utils/Singleton.h
        Utils/SnowflakeID.h
        Utils/base64.h

        # Agent - AI Agent
        Agent/AgentManager.cpp
        Agent/AgentManager.h
        Agent/AgentRuntime.cpp
        Agent/AgentRuntime.h

        # DTO
        DTO/AuthDTO.h
        DTO/FriendDTO.h
        DTO/GroupDTO.h
        DTO/JsonUtils.h
        DTO/SyncDTO.h

        # Repository
        Repository/GroupRepository.cpp
        Repository/GroupRepository.h

        # Service
        Service/GroupService.cpp
        Service/GroupService.h
        Service/GroupMemberService.cpp
        Service/GroupMemberService.h
        Service/GroupMessageService.cpp
        Service/GroupMessageService.h

        # Proto - Protobuf 生成文件
        Proto/message.pb.cc
        Proto/message.grpc.pb.cc
)



# =========================================================
# 4. 构建与链接
# =========================================================
add_executable(ChatServer ${SOURCE_FILES})

target_include_directories(ChatServer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Network
        ${CMAKE_CURRENT_SOURCE_DIR}/Grpc
        ${CMAKE_CURRENT_SOURCE_DIR}/Database
        ${CMAKE_CURRENT_SOURCE_DIR}/Logic
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils
        ${CMAKE_CURRENT_SOURCE_DIR}/Proto
        ${CMAKE_CURRENT_SOURCE_DIR}/Agent
        ${CMAKE_CURRENT_SOURCE_DIR}/DTO
        ${CMAKE_CURRENT_SOURCE_DIR}/Repository
        ${CMAKE_CURRENT_SOURCE_DIR}/Service
)

target_link_libraries(ChatServer
        PRIVATE
        protobuf::libprotobuf
        gRPC::grpc++
        JsonCpp::JsonCpp
        Boost::boost Boost::filesystem
        hiredis::hiredis
        unofficial::mysql-connector-cpp::connector
        OpenSSL::SSL OpenSSL::Crypto

        # 【关键修复】链接 MySQL 的底层依赖
        lz4::lz4
        $<IF:$<TARGET_EXISTS:zstd::libzstd_static>,zstd::libzstd_static,zstd::libzstd_shared>

        # Windows 系统库
        ws2_32 Crypt32 Secur32
)

# 复制配置文件
add_custom_command(TARGET ChatServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.ini
        $<TARGET_FILE_DIR:ChatServer>/config.ini
        COMMENT "Copying config.ini..."
)
