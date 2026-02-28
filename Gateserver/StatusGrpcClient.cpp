//
// Created by EDPerson on 2026/2/11.
//

#include "StatusGrpcClient.h"

StatusGrpcClient::StatusGrpcClient() {
    // 从配置读取StatusServer地址
    auto& cfg = ConfigMgr::Inst();
    std::string host = cfg["StatusServer"]["Host"];
    std::string port = cfg["StatusServer"]["Port"];
    std::string address = host + ":" + port;
    
    // 创建连接池（3个连接）
    _pool = std::make_unique<StatusConPool>(address, 3);
    
    std::cout << "StatusGrpcClient初始化完成，连接到: " << address << std::endl;
}

StatusGrpcClient::~StatusGrpcClient() {
    std::cout << "StatusGrpcClient析构" << std::endl;
}

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid) {
    // 从连接池获取stub
    auto stub = _pool->getConnection();
    if (!stub) {
        GetChatServerRsp reply;
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }
    
    ClientContext context;
    GetChatServerRsp response;
    GetChatServerReq request;
    request.set_uid(uid);
    
    // 调用gRPC服务
    Status status = stub->GetChatServer(&context, request, &response);
    
    if (status.ok()) {
        std::cout << "GetChatServer成功: uid=" << uid 
                  << ", host=" << response.host() 
                  << ", port=" << response.port() 
                  << ", token=" << response.token() << std::endl;
        return response;
    } else {
        std::cerr << "GetChatServer失败: " << status.error_message() << std::endl;
        response.set_error(ErrorCodes::RPCFailed);
        return response;
    }
}
