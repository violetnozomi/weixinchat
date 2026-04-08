# WeixinChat —— 仿微信 IM 即时通讯系统

基于 C++ / Qt 的全栈即时通讯项目，涵盖文字聊天、文件传输、实时音视频通话以及 AI 智能助手，采用多服务分布式架构。

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         Qt 客户端                                │
│  登录/注册  │  好友管理  │  聊天窗口  │  音视频通话  │  文件传输  │
└──────┬──────────────────────────────────────┬────────────────────┘
       │ HTTP REST                             │ TCP 长连接 (自定义协议)
       ▼                                       ▼
┌─────────────┐   gRPC    ┌────────────────────────────────────┐
│  GateServer │ ────────► │           ChatServer               │
│  (认证/路由) │           │  - Boost.Asio 网络层               │
└──────┬──────┘           │  - 逻辑线程池 + Session strand     │
       │ gRPC             │  - MinIO 文件传输                  │
       ▼                  │  - WebRTC 信令中转                 │
┌──────────────┐          │  - AI Agent (OpenAI Compatible)    │
│GrpcStatusSvr │          └───────────────┬────────────────────┘
│  (负载均衡)  │                           │
└──────┬───────┘               ┌──────────┴────────┐
       │ gRPC                  │                   │
       ▼                       ▼                   ▼
┌─────────────┐         ┌─────────────┐    ┌─────────────┐
│ VarifyServer│         │    MySQL    │    │    Redis    │
│  (邮件验证) │         │  (持久存储) │    │  (Token/缓存)│
│  Node.js    │         └─────────────┘    └─────────────┘
└─────────────┘
                                    ┌─────────────┐
                                    │    MinIO    │
                                    │ (对象存储)  │
                                    └─────────────┘
```

### 各服务职责

| 服务 | 语言 | 职责 |
|------|------|------|
| **GateServer** | C++ / Boost.Beast | HTTP 接入层：注册、登录、搜索用户、密码重置；验证完成后将 ChatServer 地址下发给客户端 |
| **ChatServer** | C++ / Boost.Asio | TCP 长连接核心：消息收发、好友管理、文件传输协调、WebRTC 信令中转、AI Agent |
| **GrpcStatusServer** | C++ / gRPC | ChatServer 注册中心与负载均衡，维护在线用户 Token |
| **VarifyServer** | Node.js | 邮件验证码发送（nodemailer + Redis TTL） |
| **Client** | C++ / Qt 6 | 桌面客户端，集成 WebRTC 音视频、MinIO 直传、聊天气泡 UI |

---

## 核心特性

- **高性能异步网络**：ChatServer 网络层（Boost.Asio io_context 池）与逻辑层（独立 thread_pool）彻底解耦，每个 Session 持有独立 `strand<any_io_executor>`，无锁串行处理，多核并发。
- **DB 读写分离**：`DbReadQueue` / `DbWriteQueue` 将阻塞 MySQL 操作移出业务线程，续体通过 `PostTask` 安全投回 Session strand。
- **MinIO 文件传输**：自实现 AWS Signature V4 预签名 URL；小文件（≤20 MB）单 PUT 直传，大文件（>20 MB）分段上传，支持断点续传（MD5 校验防同名异体）。
- **实时音视频**：集成 libwebrtc，PeerConnection 三线程模型（network / worker / signaling），信令通过 TCP 长连接中转，支持 ICE / STUN 穿透，纯语音与音视频双模式。
- **AI 智能助手**：内置 `AgentManager`，支持 OpenAI Compatible API（两阶段：Tool Call + SSE 流式推送），带对话记忆池（LRU 限制 + MySQL 持久化）。

---

## 依赖环境

### 服务端（ChatServer / GateServer / GrpcStatusServer）

| 依赖 | 最低版本 | 说明 |
|------|---------|------|
| CMake | 3.20 | 构建系统 |
| Boost | 1.82 | Asio、Beast、UUID |
| gRPC + Protobuf | 1.54 | 服务间 RPC |
| JsonCpp | 1.9 | JSON 解析 |
| MySQL Connector/C++ | 8.0 (X DevAPI) | 数据库 |
| hiredis / redis-plus-plus | — | Redis 客户端 |
| OpenSSL | 1.1+ | TLS（AI Agent HTTPS） |
| libcurl | 7.x | MinIO HTTP 请求 |

### 客户端

| 依赖 | 版本 | 说明 |
|------|------|------|
| Qt | 6.x | UI 框架 |
| libwebrtc | M114+ | 音视频 |
| OpenSSL | 1.1+ | TLS |

### 中间件

- **MySQL** 8.0+（端口 33060，X Protocol）
- **Redis** 6.0+
- **MinIO** 最新稳定版

### VarifyServer

- Node.js 18+
- 依赖见 `VarifyServer/package.json`，运行 `npm install` 安装

---

## 快速开始

### 1. 准备配置文件

每个服务目录下均有 `config.ini.example`，复制并填入真实值：

```bash
cp ChatServer/config.ini.example      ChatServer/config.ini
cp Gateserver/config.ini.example      Gateserver/config.ini
cp GrpcStatusServer/config.ini.example GrpcStatusServer/config.ini
cp VarifyServer/config.ini.example    VarifyServer/config.ini
```

> **注意**：`config.ini` 已加入 `.gitignore`，不会被提交到版本库。

### 2. 初始化数据库

```sql
-- 执行 GrpcStatusServer/init_database.sql 创建表结构
-- 执行 ChatServer/migrate_chat_messages.sql
-- 执行 ChatServer/migrate_file_transfers.sql
```

### 3. 构建服务端

```bash
# 以 ChatServer 为例（GateServer / GrpcStatusServer 步骤相同）
cd ChatServer
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 4. 启动服务（顺序）

```
1. MySQL + Redis + MinIO  （基础设施）
2. VarifyServer           node VarifyServer/server.js
3. GrpcStatusServer       ./build/GrpcStatusServer
4. GateServer             ./build/GateServer
5. ChatServer             ./build/ChatServer
6. Client                 （Qt 编译后运行）
```

### 5. 构建客户端

用 Qt Creator 打开 `client/client.pro`，选择 Release 构建即可。

---

## 消息协议

客户端与 ChatServer 之间使用自定义二进制 TCP 协议：

```
┌────────────┬────────────┬──────────────────┐
│  msg_id    │  msg_len   │     JSON body    │
│  (2 bytes) │  (2 bytes) │  (msg_len bytes) │
│  大端序    │  大端序    │                  │
└────────────┴────────────┴──────────────────┘
```

消息 ID 分区（详见 `ChatServer/const.h`）：

| 范围 | 用途 |
|------|------|
| 2001–2013 | 登录认证、文字消息、好友管理 |
| 2023–2028 | 删除/屏蔽好友、修改资料 |
| 2030–2036 | MinIO 小文件传输 |
| 2037–2043 | MinIO 分段上传（断点续传） |
| 2999 | 心跳 |
| 3001 | WebRTC 信令 |

---

## 目录结构

```
weixinchat1/
├── ChatServer/          # 核心聊天服务（C++ / Boost.Asio）
├── Gateserver/          # HTTP 接入层（C++ / Boost.Beast）
├── GrpcStatusServer/    # 状态/负载均衡服务（C++ / gRPC）
├── VarifyServer/        # 邮件验证码服务（Node.js）
├── client/              # Qt 桌面客户端
│   ├── Manager/         # 业务管理器（Chat/Friend/User/File/WebRTC）
│   ├── Network/         # TCP 网络层（TcpMgr / HttpMgr）
│   └── UI/              # 所有界面组件
└── IM_Files_Data/       # MinIO 本地数据目录（开发环境）
```

---

## 安全说明

- 所有密码均通过 MD5 哈希后存储
- config.ini 文件包含敏感凭据，已通过 `.gitignore` 排除在版本库之外
- MinIO 预签名 URL 有效期：上传 1 小时，下载 7 天
- ChatServer 具备认证超时（10s）和空闲超时（90s）双重防护

---

## 简历参考描述

> 以下内容可直接参考用于个人简历，根据实际岗位方向按需取舍。

---

### 项目名称

**WeixinChat —— 基于 C++ 的全栈 IM 即时通讯系统**

**技术栈：** C++17 / Qt 6 / Boost.Asio / gRPC / Protobuf / WebRTC / MinIO / Redis / MySQL / Node.js

---

1

## License

本项目为个人学习项目，仅供参考。
