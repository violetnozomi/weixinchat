cmake_minimum_required(VERSION 3.20)
project(GrpcStatusServer)

set(CMAKE_CXX_STANDARD 17)

# =========================================================
# 1. 全局配置 (模仿 wxchat 的写法)
# =========================================================
if (MSVC)
    # 强制 UTF-8 编码
    add_compile_options(/utf-8)

    # 定义 Windows 版本
    add_definitions(-D_WIN32_WINNT=0x0A00)

    # 【重要补丁】禁用 Windows 的 min/max 宏，防止 gRPC/ProtoBuf 编译报错
    # 你的 wxchat 可能没碰到这个问题，但 GrpcStatusServer 的日志里报错了，所以必须加！
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)

    # 设置静态运行时 (/MT 和 /MTd)，匹配 vcpkg 的 x64-windows-static
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# =========================================================
# 2. 查找依赖库 (根据你的环境修正)
# =========================================================
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system)

# 你的环境里文件夹叫 hiredis
find_package(hiredis CONFIG REQUIRED)

# 你的环境里文件夹叫 mysql-connector-cpp，但配置名通常是 unofficial
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)

# 静态链接 MySQL 需要的依赖
find_package(lz4 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

# =========================================================
# 3. 定义源文件
# =========================================================
set(SOURCE_FILES
        main.cpp
        const.h

        # 业务逻辑
        StatusServiceImpl.cpp StatusServiceImpl.h

        # 数据库与配置
        ConfigMgr.cpp ConfigMgr.h
        RedisMgr.cpp RedisMgr.h
        RedisconPool.cpp RedisconPool.h
        MySqlPool.cpp MySqlPool.h

        # 密码哈希工具（SHA-256 + 盐值）
        HashUtils.cpp HashUtils.h

        # 网络与工具
        IoServicePool.cpp IoServicePool.h

        # ProtoBuf 生成的文件
        message.pb.cc message.pb.h
        message.grpc.pb.cc message.grpc.pb.h
)

# =========================================================
# 4. 构建与链接 (先创建目标，再配置)
# =========================================================
add_executable(GrpcStatusServer ${SOURCE_FILES})

target_include_directories(GrpcStatusServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(GrpcStatusServer
        PRIVATE
        protobuf::libprotobuf
        gRPC::grpc++

        JsonCpp::JsonCpp
        Boost::boost Boost::filesystem

        # Hiredis (根据之前的截图)
        hiredis::hiredis

        # MySQL (根据 vcpkg 惯例)
        unofficial::mysql-connector-cpp::connector

        # MySQL 底层依赖 (模仿 wxchat 的写法)
        lz4::lz4
        $<IF:$<TARGET_EXISTS:zstd::libzstd_static>,zstd::libzstd_static,zstd::libzstd_shared>

        # Windows 系统库 (静态链接必须加)
        ws2_32 Crypt32 Secur32 Bcrypt
)

# =========================================================
# 5. 复制配置文件
# =========================================================
add_custom_command(TARGET GrpcStatusServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.ini
        $<TARGET_FILE_DIR:GrpcStatusServer>/config.ini
        COMMENT "Copying config.ini..."
)