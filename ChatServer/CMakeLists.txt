cmake_minimum_required(VERSION 3.20)
project(ChatServer)

set(CMAKE_CXX_STANDARD 17)

# =========================================================
# 1. 修复 LNK2038：强制使用静态运行时 (MT/MTd)
# =========================================================
if (MSVC)
    add_compile_options(/utf-8)
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
# 3. 定义源文件
# =========================================================
set(SOURCE_FILES
        main.cpp
        CServer.cpp
        CServer.h
        const.h
        CSession.cpp
        CSession.h
        Singleton.h
        IOservicePool.cpp
        IOservicePool.h
        ChatLogicSystem.cpp
        ChatLogicSystem.h
        SessionManager.cpp
        SessionManager.h
        StatusGrpcClient.cpp
        StatusGrpcClient.h
        ChatGrpcClient.cpp
        ChatGrpcClient.h
        ChatServiceImpl.cpp
        ChatServiceImpl.h
        GrpcStubPool.h
        MySqlPool.cpp
        MySqlPool.h
        MinioMgr.cpp
        MinioMgr.h
        RedisMgr.cpp
        RedisMgr.h
        RedisconPool.cpp
        RedisconPool.h
        ConfigMgr.cpp
        ConfigMgr.h
        message.pb.cc
        message.grpc.pb.cc
)



# =========================================================
# 4. 构建与链接
# =========================================================
add_executable(ChatServer ${SOURCE_FILES})

target_include_directories(ChatServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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