//
// Created by EDPerson on 2026/2/13.
//

#include "StatusGrpcClient.h"
#include "ConfigMgr.h"
#include <iostream>

StatusGrpcClient::StatusGrpcClient() {
    auto& cfg    = ConfigMgr::Inst();
    std::string addr = cfg["StatusServer"]["Host"] + ":" + cfg["StatusServer"]["Port"];
    int pool_size = 5;
    try { pool_size = std::stoi(cfg["StatusServer"]["PoolSize"]); } catch (...) {}

    _pool = std::make_unique<StatusPool>(
        pool_size, addr,
        [](std::shared_ptr<grpc::Channel> ch) {
            return StatusService::NewStub(ch);
        });
    std::cout << "[StatusGrpcClient] pool=" << pool_size << " -> " << addr << std::endl;
}

LoginVerifyRsp StatusGrpcClient::LoginVerify(const std::string& token, int uid) {
    std::cout << "[GRPC] LoginVerify uid=" << uid << std::endl;
    auto g = _pool->acquire();
    if (!g.valid()) {
        LoginVerifyRsp r;  r.set_error(ErrorCodes::RPCFailed);  r.set_msg("pool exhausted");  return r;
    }
    ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    LoginVerifyReq req;  req.set_token(token);  req.set_uid(uid);
    LoginVerifyRsp rsp;
    Status st = g->LoginVerify(&ctx, req, &rsp);
    if (!st.ok()) {
        std::cerr << "[GRPC] LoginVerify failed: " << st.error_message() << std::endl;
        rsp.set_error(ErrorCodes::RPCFailed);  rsp.set_msg("RPC call failed");
    }
    return rsp;
}

GetUserServerRsp StatusGrpcClient::GetUserServer(int uid) {
    auto g = _pool->acquire();
    if (!g.valid()) {
        GetUserServerRsp r;  r.set_error(ErrorCodes::RPCFailed);  return r;
    }
    ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    GetUserServerReq req;  req.set_uid(uid);
    GetUserServerRsp rsp;
    Status st = g->GetUserServer(&ctx, req, &rsp);
    if (!st.ok()) {
        std::cerr << "[GRPC] GetUserServer failed: " << st.error_message() << std::endl;
        rsp.set_error(ErrorCodes::RPCFailed);
    }
    return rsp;
}
