#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include <iostream>

ChatGrpcClient::ChatGrpcClient() {
    try {
        _per_addr_pool_size = std::stoi(
            ConfigMgr::Inst()["ChatServer"]["ChatPoolSize"]);
    } catch (...) {
        _per_addr_pool_size = 3;
    }
    std::cout << "[ChatGrpcClient] per-address pool size=" << _per_addr_pool_size << std::endl;
}

ChatGrpcClient::ChatPool&
ChatGrpcClient::getOrCreatePool(const std::string& address) {
    std::lock_guard<std::mutex> lk(_map_mutex);
    auto it = _pools.find(address);
    if (it != _pools.end()) return *it->second;

    // 新地址：建立连接池
    auto pool = std::make_unique<ChatPool>(
        _per_addr_pool_size, address,
        [](std::shared_ptr<grpc::Channel> ch) {
            return ChatService::NewStub(ch);
        });
    auto& ref = *pool;
    _pools.emplace(address, std::move(pool));
    return ref;
}

DeliverAddFriendRsp ChatGrpcClient::DeliverAddFriend(
    const std::string& host,
    const std::string& grpc_port,
    int from_uid,
    const std::string& from_name,
    int to_uid,
    const std::string& message)
{
    std::string address = host + ":" + grpc_port;
    auto& pool = getOrCreatePool(address);
    auto  g    = pool.acquire();

    DeliverAddFriendRsp response;
    if (!g.valid()) {
        std::cerr << "[ChatGrpcClient] pool exhausted for " << address << std::endl;
        response.set_error(ErrorCodes::RPCFailed);
        response.set_msg("gRPC pool exhausted");
        return response;
    }

    ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + std::chrono::seconds(5));

    DeliverAddFriendReq request;
    request.set_from_uid(from_uid);
    request.set_from_name(from_name);
    request.set_to_uid(to_uid);
    request.set_message(message);

    Status status = g->DeliverAddFriend(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "[ChatGrpcClient] DeliverAddFriend failed to " << address
                  << ": " << status.error_message() << std::endl;
        response.set_error(ErrorCodes::RPCFailed);
        response.set_msg("gRPC call failed");
    }
    return response;
}
DeliverSignalingRsp ChatGrpcClient::DeliverWebRTCSignaling(
    const std::string& host,
    const std::string& grpc_port,
    int from_uid,
    int to_uid,
    const std::string& type,
    const std::string& payload)
{
    std::string address = host + ":" + grpc_port;
    auto& pool = getOrCreatePool(address);
    auto stub = pool.acquire();// 从池子里借一个连接

    DeliverSignalingReq req;
    req.set_from_uid(from_uid);
    req.set_to_uid(to_uid);
    req.set_type(type);
    req.set_payload(payload);

    DeliverSignalingRsp rsp;
    ClientContext context;

    // 执行 RPC 调用
    Status status = stub->DeliverWebRTCSignaling(&context, req, &rsp);

    if (!status.ok()) {
        std::cout << "gRPC DeliverWebRTCSignaling failed:" << status.error_message().c_str()<<std::endl;
        rsp.set_error(ErrorCodes::RPCFailed); // 预定义的网络错误码

    }

    return rsp;
}