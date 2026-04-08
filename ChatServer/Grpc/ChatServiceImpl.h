#ifndef CHATSERVER_CHATSERVICEIMPL_H
#define CHATSERVER_CHATSERVICEIMPL_H

#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "const.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::ChatService;
using message::DeliverAddFriendReq;
using message::DeliverAddFriendRsp;
using message::DeliverSignalingReq;
using message::DeliverSignalingRsp;

// ChatServer 对外暴露的 gRPC 服务（供其他 ChatServer 通过 gRPC 调用本机推送）
class ChatServiceImpl : public ChatService::Service {
public:
    // 接收其他 ChatServer 转发来的添加好友请求，推送给本地在线用户
    Status DeliverAddFriend(ServerContext* context,
                            const DeliverAddFriendReq* request,
                            DeliverAddFriendRsp* reply) override;
    Status DeliverWebRTCSignaling(ServerContext* context,
                                  const DeliverSignalingReq* request,
                                  DeliverSignalingRsp* reply) override;
};

#endif //CHATSERVER_CHATSERVICEIMPL_H
