//
// Created by EDPerson on 2026/2/12.
//

#ifndef WXCHAT_STATUSSERVICEIMPL_H
#define WXCHAT_STATUSSERVICEIMPL_H

#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <boost/asio.hpp>
#include <boost/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include "ConfigMgr.h"
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginVerifyReq;
using message::LoginVerifyRsp;
using message::GetUserServerReq;
using message::GetUserServerRsp;
using message::StatusService;
struct ChatServer {
    std::string host;
    std::string port;      // TCP 聊天端口
    std::string grpc_port; // gRPC 跨服端口
};
class StatusServiceImpl:public StatusService::Service {
public:
    StatusServiceImpl();
    ~StatusServiceImpl();
    Status GetChatServer(ServerContext* context, const GetChatServerReq* request,GetChatServerRsp* reply) override;
    Status LoginVerify(ServerContext* context, const LoginVerifyReq* request, LoginVerifyRsp* reply) override;
    Status GetUserServer(ServerContext* context, const GetUserServerReq* request, GetUserServerRsp* reply) override;
    std::vector<ChatServer> _chatServers;
    int _chatserver_index;
    std:: string Get_uuid();
};


#endif //WXCHAT_STATUSSERVICEIMPL_H