//
// Created by EDPerson on 2026/2/12.
//

#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include "MySqlPool.h"
#include <iostream>
StatusServiceImpl::StatusServiceImpl():_chatserver_index(0) {
    auto cfg = ConfigMgr::Inst();
    ChatServer chatserver;
    chatserver.host      = cfg["ChatServer1"]["Host"];
    chatserver.port      = cfg["ChatServer1"]["Port"];
    chatserver.grpc_port = cfg["ChatServer1"]["GrpcPort"];   // 新增 gRPC 端口
    std::cout << "ChatServer1配置: host=" << chatserver.host
              << ", port=" << chatserver.port
              << ", grpc_port=" << chatserver.grpc_port << std::endl;
    _chatServers.push_back(chatserver);
}
Status StatusServiceImpl::GetChatServer(ServerContext *context, const GetChatServerReq *request, GetChatServerRsp *reply) {
    std::cout << "GetChatServer request: uid=" << request->uid() << std::endl;
    
    // 轮询选择ChatServer
    _chatserver_index = (_chatserver_index + 1) % _chatServers.size();
    auto chatserver = _chatServers[_chatserver_index];
    
    // 生成token
    std::string token = Get_uuid();
    
    // 将token存储到Redis，关联uid，设置7天过期
    std::string token_key = "token:" + token;
    std::string uid_str = std::to_string(request->uid());
    
    bool set_result = RedisMgr::GetInstance()->Set(token_key, uid_str);
    if (!set_result) {
        std::cerr << "Failed to store token in Redis" << std::endl;
        reply->set_error(ErrorCodes::RPCFailed);
        return Status::OK;
    }

    // 将 uid → "host:grpc_port" 存入 Redis，供跨服添加好友时路由
    std::string server_key = "uid_server:" + uid_str;
    std::string server_val = chatserver.host + ":" + chatserver.grpc_port;
    RedisMgr::GetInstance()->Set(server_key, server_val);
    std::cout << "uid_server:" << uid_str << " = " << server_val << std::endl;
    
    std::cout << "Generated token for uid=" << request->uid() 
              << ", token=" << token << std::endl;
    
    // 返回结果
    std::cout << "返回ChatServer: host=" << chatserver.host << ", port=" << chatserver.port << std::endl;
    reply->set_host(chatserver.host);
    reply->set_port(chatserver.port);
    reply->set_error(ErrorCodes::Success);
    reply->set_token(token);
    
    return Status::OK;
}

Status StatusServiceImpl::LoginVerify(ServerContext *context, const LoginVerifyReq *request, LoginVerifyRsp *reply) {
    std::string token = request->token();
    int uid = request->uid();
    
    std::cout << "[LoginVerify] Request received: uid=" << uid << ", token=" << token << std::endl;
    
    // 从 Redis 中验证 token
    std::string token_key = "token:" + token;
    std::string stored_uid;
    
    std::cout << "[LoginVerify] Checking token in Redis..." << std::endl;
    bool get_result = RedisMgr::GetInstance()->Get(token_key, stored_uid);
    
    if (!get_result || stored_uid.empty()) {
        // Token不存在或已过期
        std::cout << "[LoginVerify] Failed: token not found or expired" << std::endl;
        reply->set_error(ErrorCodes::PasswdInvalid);
        reply->set_msg("Invalid or expired token");
        return Status::OK;
    }
    
    std::cout << "[LoginVerify] Token found in Redis, stored_uid=" << stored_uid << std::endl;
    
    // 验证 uid 是否匹配
    if (std::to_string(uid) != stored_uid) {
        std::cout << "[LoginVerify] Failed: uid mismatch, expected=" << stored_uid 
                  << ", got=" << uid << std::endl;
        reply->set_error(ErrorCodes::PasswdInvalid);
        reply->set_msg("Token and uid mismatch");
        return Status::OK;
    }
    
    std::cout << "[LoginVerify] UID matches, querying database..." << std::endl;
    
    //从数据库查询用户信息，获取用户名
    MysqlDao dao;
    UserInfo userInfo;
    std::cout << "[LoginVerify] Calling dao.GetUserByUid..." << std::endl;
    
    if (!dao.GetUserByUid(uid, userInfo)) {
        std::cerr << "[LoginVerify] Failed: user not found in database, uid=" << uid << std::endl;
        reply->set_error(ErrorCodes::UserNotExist);
        reply->set_msg("User not found");
        return Status::OK;
    }
    
    std::cout << "[LoginVerify] Database query successful, user=" << userInfo.name << std::endl;
    std::cout << "[LoginVerify] Success: uid=" << uid << ", user=" << userInfo.name << std::endl;
    
    reply->set_error(ErrorCodes::Success);
    reply->set_msg("Token verified successfully");
    reply->set_uid(uid);
    reply->set_user(userInfo.name);  // 返回用户名
    
    std::cout << "[LoginVerify] Sending response..." << std::endl;
    return Status::OK;
}

// 查询 uid 当前所在的 ChatServer（gRPC 地址）
Status StatusServiceImpl::GetUserServer(ServerContext* /*context*/,
                                        const GetUserServerReq* request,
                                        GetUserServerRsp* reply)
{
    int uid = request->uid();
    std::string key = "uid_server:" + std::to_string(uid);
    std::string server_val;

    bool ok = RedisMgr::GetInstance()->Get(key, server_val);
    if (!ok || server_val.empty()) {
        std::cerr << "[GetUserServer] uid=" << uid << " not found in Redis (offline?)" << std::endl;
        reply->set_error(ErrorCodes::UserOffline);
        return Status::OK;
    }

    // server_val 格式: "host:grpc_port"
    auto sep = server_val.find(':');
    if (sep == std::string::npos) {
        reply->set_error(ErrorCodes::RPCFailed);
        return Status::OK;
    }
    reply->set_host(server_val.substr(0, sep));
    reply->set_grpc_port(server_val.substr(sep + 1));
    reply->set_error(ErrorCodes::Success);
    std::cout << "[GetUserServer] uid=" << uid << " → " << server_val << std::endl;
    return Status::OK;
}

std::string StatusServiceImpl::Get_uuid() {
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    return boost::uuids::to_string(uuid);
}
StatusServiceImpl::~StatusServiceImpl() {

}
