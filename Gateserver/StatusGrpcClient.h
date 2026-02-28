//
// Created by EDPerson on 2026/2/11.
//

#ifndef WXCHAT_TATUSGRPCCLIENT_H
#define WXCHAT_TATUSGRPCCLIENT_H
#include <absl/status/status.h>
#include <grpcpp/impl/codegen/config_protobuf.h>
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "const.h"
#include "Singleton.h"
#include "ConfigMgr.h"
#include "GrpcConnectionPool.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::StatusService;

class StatusGrpcClient :public Singleton<StatusGrpcClient>{
    friend class Singleton<StatusGrpcClient>;
public:
    ~StatusGrpcClient();
    GetChatServerRsp GetChatServer(int uid);
private:
    StatusGrpcClient();
    std::unique_ptr<StatusConPool> _pool;

};


#endif //WXCHAT_TATUSGRPCCLIENT_H