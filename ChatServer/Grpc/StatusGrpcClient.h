//
// Created by EDPerson on 2026/2/13.
//

#ifndef CHATSERVER_STATUSGRPCCLIENT_H
#define CHATSERVER_STATUSGRPCCLIENT_H

#include "const.h"
#include "Singleton.h"
#include "GrpcStubPool.h"
#include "ConfigMgr.h"
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <memory>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using message::LoginVerifyReq;
using message::LoginVerifyRsp;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::GetUserServerReq;
using message::GetUserServerRsp;
using message::StatusService;

class StatusGrpcClient : public Singleton<StatusGrpcClient> {
    friend class Singleton<StatusGrpcClient>;
public:
    ~StatusGrpcClient() = default;

    LoginVerifyRsp   LoginVerify(const std::string& token, int uid);
    GetUserServerRsp GetUserServer(int uid);

private:
    StatusGrpcClient();

    using StatusPool = GrpcStubPool<StatusService::Stub>;
    std::unique_ptr<StatusPool> _pool;
};

#endif //CHATSERVER_STATUSGRPCCLIENT_H
