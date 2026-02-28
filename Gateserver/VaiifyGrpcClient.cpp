//
// Created by EDPerson on 2026/2/5.
//

#include "VaiifyGrpcClient.h"
#include "ConfigMgr.h"

VaiifyGrpcClient::VaiifyGrpcClient() {
    // 从配置读取gRPC服务器地址
    auto& cfg = ConfigMgr::Inst();
    std::string host = cfg["VarifyServer"]["Host"];
    std::string port = cfg["VarifyServer"]["Port"];
    std::string address = host + ":" + port;
    
    // 创建连接池（3个连接）
    _pool = std::make_unique<GrpcConnectionPool>(address, 3);
    
    std::cout << "VaiifyGrpcClient初始化完成，连接到: " << address << std::endl;
}
