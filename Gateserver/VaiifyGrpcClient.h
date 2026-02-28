//
// Created by EDPerson on 2026/2/5.
//

#ifndef WXCHAT_VAIIFYGRPCCLIENT_H
#define WXCHAT_VAIIFYGRPCCLIENT_H
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "const.h"
#include "Singleton.h"
#include "GrpcConnectionPool.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetVarifyReq;
using message::GetVarifyRsp;
using message::VarifyService;

class VaiifyGrpcClient:public Singleton<VaiifyGrpcClient> {
    friend class Singleton<VaiifyGrpcClient>;
public:
    GetVarifyRsp GetVarifyCode(std::string email) {
        // 从连接池获取stub
        auto stub = _pool->getStub();
        if (!stub) {
            GetVarifyRsp reply;
            reply.set_error(ErrorCodes::RPCFailed);
            return reply;
        }
        
        ClientContext context;
        GetVarifyRsp reply;
        GetVarifyReq request;
        request.set_email(email);
        
        // 使用连接池中的stub
        Status status = stub->GetVarifyCode(&context, request, &reply);
        if (status.ok()) {
            return reply;
        } else {
            std::cerr << "gRPC调用失败: " << status.error_message() << std::endl;
            reply.set_error(ErrorCodes::RPCFailed);
            return reply;
        }
    }
    
private:
    VaiifyGrpcClient();
    std::unique_ptr<GrpcConnectionPool> _pool;
};


#endif //WXCHAT_VAIIFYGRPCCLIENT_H