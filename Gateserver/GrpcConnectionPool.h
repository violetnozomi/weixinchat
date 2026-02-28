//
// Created by EDPerson on 2026/2/9.
//

#ifndef WXCHAT_GRPCCONNECTIONPOOL_H
#define WXCHAT_GRPCCONNECTIONPOOL_H

#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "const.h"
#include <vector>
#include <memory>
#include <atomic>

using grpc::Channel;
using message::VarifyService;
using message::StatusService;

// gRPC连接池（轮询分配）
class GrpcConnectionPool {
public:
    GrpcConnectionPool(const std::string& host, size_t pool_size = 3);
    
    // 获取一个stub（轮询）
    std::shared_ptr<VarifyService::Stub> getStub();
    
    // 获取可用连接数
    size_t getPoolSize() const { return _stubs.size(); }
    
private:
    std::vector<std::shared_ptr<VarifyService::Stub>> _stubs;
    std::atomic<size_t> _next_index;
};

// StatusService连接池（轮询分配）
class StatusConPool {
public:
    StatusConPool(const std::string& host, size_t pool_size = 3);
    
    // 获取一个stub（轮询）
    std::shared_ptr<StatusService::Stub> getConnection();
    
    // 获取可用连接数
    size_t getPoolSize() const { return _stubs.size(); }
    
private:
    std::vector<std::shared_ptr<StatusService::Stub>> _stubs;
    std::atomic<size_t> _next_index;
};

#endif //WXCHAT_GRPCCONNECTIONPOOL_H
