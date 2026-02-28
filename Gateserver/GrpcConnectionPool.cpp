//
// Created by EDPerson on 2026/2/9.
//

#include "GrpcConnectionPool.h"
#include <iostream>

GrpcConnectionPool::GrpcConnectionPool(const std::string& host, size_t pool_size)
    : _next_index(0) {
    
    // 创建多个Channel和Stub
    for (size_t i = 0; i < pool_size; ++i) {
        // 每个Channel都是独立的连接
        auto channel = grpc::CreateChannel(host, grpc::InsecureChannelCredentials());
        
        // 可选：等待连接就绪
        if (channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(5))) {
            auto stub = VarifyService::NewStub(channel);
            _stubs.push_back(std::move(stub));
            std::cout << "gRPC连接池: 连接 " << i << " 创建成功" << std::endl;
        } else {
            std::cerr << "gRPC连接池: 连接 " << i << " 创建失败" << std::endl;
        }
    }
    
    if (_stubs.empty()) {
        throw std::runtime_error("gRPC连接池初始化失败：没有可用连接");
    }
    
    std::cout << "gRPC连接池初始化完成，可用连接数: " << _stubs.size() << std::endl;
}

std::shared_ptr<VarifyService::Stub> GrpcConnectionPool::getStub() {
    if (_stubs.empty()) {
        return nullptr;
    }
    
    // 原子操作实现轮询
    auto index = _next_index.fetch_add(1) % _stubs.size();
    return _stubs[index];
}

// ===================== StatusConPool 实现 =====================

StatusConPool::StatusConPool(const std::string& host, size_t pool_size)
    : _next_index(0) {
    
    // 创建多个Channel和Stub
    for (size_t i = 0; i < pool_size; ++i) {
        // 每个Channel都是独立的连接
        auto channel = grpc::CreateChannel(host, grpc::InsecureChannelCredentials());
        
        // 可选：等待连接就绪
        if (channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(5))) {
            auto stub = StatusService::NewStub(channel);
            _stubs.push_back(std::move(stub));
            std::cout << "StatusService连接池: 连接 " << i << " 创建成功" << std::endl;
        } else {
            std::cerr << "StatusService连接池: 连接 " << i << " 创建失败" << std::endl;
        }
    }
    
    if (_stubs.empty()) {
        throw std::runtime_error("StatusService连接池初始化失败：没有可用连接");
    }
    
    std::cout << "StatusService连接池初始化完成，可用连接数: " << _stubs.size() << std::endl;
}

std::shared_ptr<StatusService::Stub> StatusConPool::getConnection() {
    if (_stubs.empty()) {
        return nullptr;
    }
    
    // 原子操作实现轮询
    auto index = _next_index.fetch_add(1) % _stubs.size();
    return _stubs[index];
}
