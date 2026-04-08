#ifndef CHATSERVER_CHATGRPCCLIENT_H
#define CHATSERVER_CHATGRPCCLIENT_H

#include "const.h"
#include "Singleton.h"
#include "GrpcStubPool.h"
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using message::ChatService;
using message::DeliverAddFriendReq;
using message::DeliverAddFriendRsp;
using message::DeliverSignalingReq;
using message::DeliverSignalingRsp;
// 连接到对端 ChatServer 的 gRPC 客户端
// 对每个目标地址维护一个小型连接池（懒加载）。
class ChatGrpcClient : public Singleton<ChatGrpcClient> {
    friend class Singleton<ChatGrpcClient>;
public:
    ~ChatGrpcClient() = default;

    DeliverAddFriendRsp DeliverAddFriend(
        const std::string& host,
        const std::string& grpc_port,
        int from_uid,
        const std::string& from_name,
        int to_uid,
        const std::string& message);
    // 在 ChatGrpcClient 类公共部分增加：
    DeliverSignalingRsp DeliverWebRTCSignaling(
        const std::string& host,
        const std::string& grpc_port,
        int from_uid,
        int to_uid,
        const std::string& type,
        const std::string& payload);

private:
    ChatGrpcClient();

    using ChatPool = GrpcStubPool<ChatService::Stub>;
    // 每个对端地址对应一个连接池
    ChatPool& getOrCreatePool(const std::string& address);

    int     _per_addr_pool_size{ 3 };  // 每个对端地址的连接数
    std::mutex _map_mutex;
    std::unordered_map<std::string, std::unique_ptr<ChatPool>> _pools;
};

#endif //CHATSERVER_CHATGRPCCLIENT_H
