//
// Created by EDPerson on 2026/2/13.
//

#include "ChatLogicSystem.h"
#include "CSession.h"
#include "SessionManager.h"
#include "StatusGrpcClient.h"
#include "ChatGrpcClient.h"
#include "RedisMgr.h"
#include "MySqlPool.h"
#include "MinioMgr.h"
#include "base64.h"
#include <iostream>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <json/json.h>
namespace fs = std::filesystem;

ChatLogicSystem::ChatLogicSystem() {
    // 注册认证消息处理器
    RegisterHandler(ID_CHAT_LOGIN_REQ, 
        [](std::shared_ptr<CSession> session, int msg_id, const std::string& data) {
            std::cout << "[AUTH] Handler called, data length: " << data.size() << std::endl;
            std::cout << "[AUTH] Data: " << data << std::endl;
            
            // 解析JSON请求
            Json::Value src_root;
            Json::Reader reader;
            bool parsing_successful = reader.parse(data, src_root);
            
            if (!parsing_successful) {
                std::cout << "[AUTH] JSON parse error" << std::endl;
                Json::Value response;
                response["error"] = ErrorCodes::Error_Json;
                response["msg"] = "Invalid JSON format";
                session->Send(ID_CHAT_LOGIN_RSP, response.toStyledString());
                return;
            }
            
            std::string token = src_root["token"].asString();
            int uid = src_root["uid"].asInt();
            std::string user = src_root["user"].asString();
            
            std::cout << "[AUTH] Parsed - uid=" << uid 
                      << ", user=" << user << ", token=" << token << std::endl;
            
            // 调用StatusServer验证token
            std::cout << "[AUTH] Calling StatusServer for token verification..." << std::endl;
            auto verify_rsp = StatusGrpcClient::GetInstance()->LoginVerify(token, uid);
            std::cout << "[AUTH] StatusServer response - error=" << verify_rsp.error() 
                      << ", msg=" << verify_rsp.msg() << std::endl;
            
            Json::Value response;
            if (verify_rsp.error() == ErrorCodes::Success) {
                std::string uid_str = std::to_string(uid);
                auto old_session = SessionManager::GetInstance()->GetSession(uid_str);
                if (old_session && old_session != session) {
                std::cout << "[AUTH] ⚠️ 检测到 UID:" << uid_str
                          << " 断线重连或异地登录，正在清理旧会话..." << std::endl;

                // (可选) 你可以在这里给旧连接发一条“您已被顶号”的消息
                // Json::Value kick_msg;
                // kick_msg["error"] = 1005; // 假设 1005 是顶号错误码
                // kick_msg["msg"] = "账号在其他设备登录";
                // old_session->Send(ID_CHAT_LOGIN_RSP, kick_msg.toStyledString());

                // 【核心命门】：必须调用 Close() 彻底掐断旧 Socket 并取消它的定时器！
                // 如果你的 CSession 里没有封装 Close()，你需要实现一个，里面执行 socket.close() 和 timer.cancel()
                old_session->Close();
            }
                // 认证成功：注册 session
                std::cout << "[AUTH] Token verified! Setting authenticated..." << std::endl;
                session->SetAuthenticated(std::to_string(uid), user);

                SessionManager::GetInstance()->AddSession(std::to_string(uid), session);

                // 立即发送认证响应（在任何可能阻塞的 Redis/DB 操作之前）
                response["error"] = ErrorCodes::Success;
                response["msg"] = "Authentication successful";
                session->Send(ID_CHAT_LOGIN_RSP, response.toStyledString());
                std::cout << "[AUTH] User authenticated successfully: uid=" << uid << std::endl;

                // 在 Redis 记录本机地址，供跨服路由查询（Redis 不可用时跳过，不影响登录）
                {
                    auto& cfg = ConfigMgr::Inst();
                    std::string my_host      = cfg["ChatServer"]["Host"];
                    std::string my_grpc_port = cfg["ChatServer"]["GrpcPort"];
                    std::string server_val   = my_host + ":" + my_grpc_port;
                    std::string redis_key    = "uid_server:" + std::to_string(uid);
                    try {
                        RedisMgr::GetInstance()->SetEx(redis_key, server_val, 86400);
                        std::cout << "[AUTH] Redis uid_server:" << uid << " = " << server_val << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "[AUTH] Redis SetEx skipped (Redis unavailable): " << e.what() << std::endl;
                    }
                }

                // ── 推送好友列表 ───────────────────────────────────────────────
                {
                    MysqlDao dao;
                    std::vector<UserInfo> friends;
                    if (dao.GetFriendsByUid(uid, friends)) {
                        Json::Value friends_rsp;
                        friends_rsp["error"] = ErrorCodes::Success;
                        Json::Value arr(Json::arrayValue);
                        for (const auto &f : friends) {
                            Json::Value item;
                            item["uid"]       = f.uid;
                            item["name"]      = f.name;
                            item["email"]     = f.email;
                            item["signature"] = "";   // 扩展字段，暂为空
                            arr.append(item);
                        }
                        friends_rsp["friends"] = arr;
                        session->Send(ID_FRIENDS_LIST_RSP, friends_rsp.toStyledString());
                        std::cout << "[AUTH] Pushed " << friends.size()
                                  << " friends to uid=" << uid << std::endl;
                    }
                }

                // ── 清理本用户7天前的离线消息（云端留7天）──────────────────
                {
                    try {
                        auto _sess = MysqlConPool::GetInstance()->GetSession();
                        if (!_sess) throw std::runtime_error("No DB session");
                        MysqlObj obj(std::move(_sess));
                        auto sch = obj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());

                        // a. 先下发未读的离线消息（type 0=文本 1=好友申请 2=好友已接受）
                        auto tbl = sch.getTable("offline_messages");
                        auto result = tbl.select("from_uid", "content", "type", "id")
                                         .where("to_uid = :uid AND delivered = 0")
                                         .bind("uid", uid)
                                         .execute();
                        for (auto row = result.fetchOne(); !row.isNull(); row = result.fetchOne()) {
                            int  offline_from  = static_cast<int>(row[0]);
                            std::string offline_content = row[1].get<std::string>();
                            int  offline_type  = static_cast<int>(row[2]);

                            if (offline_type == 0) {
                                // 普通文本消息
                                Json::Value offline_msg;
                                offline_msg["from_uid"]  = offline_from;
                                offline_msg["content"]   = offline_content;
                                offline_msg["timestamp"] = static_cast<Json::Int64>(time(nullptr));
                                session->Send(ID_TEXT_CHAT_MSG, offline_msg.toStyledString());
                            } else if (offline_type == 1) {
                                // 好友申请通知：content 为 JSON {from_uid, from_name, message}
                                Json::Value parsed;  Json::Reader rdr;
                                if (rdr.parse(offline_content, parsed))
                                    session->Send(ID_NOTIFY_ADD_FRIEND, parsed.toStyledString());
                            } else if (offline_type == 2) {
                                // 好友已接受通知：content 为 JSON {from_uid, from_name}
                                Json::Value parsed;  Json::Reader rdr;
                                if (rdr.parse(offline_content, parsed))
                                    session->Send(ID_NOTIFY_ACCEPT_FRIEND, parsed.toStyledString());
                            }
                            // offline_type == 3 为旧版 TCP 分块文件传输通知，已废弃，跳过
                        }
                        // 标记为已投递
                        tbl.update()
                           .set("delivered", 1)
                           .where("to_uid = :uid AND delivered = 0")
                           .bind("uid", uid)
                           .execute();

                        // b. 删除超过7天的旧消息（X DevAPI 表达式不支持 INTERVAL，用原生 SQL）
                        obj.get()->sql(
                            "DELETE FROM offline_messages "
                            "WHERE to_uid = ? AND created_at < DATE_SUB(NOW(), INTERVAL 7 DAY)"
                        ).bind(uid).execute();
                        std::cout << "[AUTH] Delivered offline messages & cleaned up >7d for uid="
                                  << uid << std::endl;
                    } catch (const std::exception &e) {
                        std::cerr << "[AUTH] Offline message handling warning: "
                                  << e.what() << std::endl;
                    }
                }

                // ── 推送离线文件记录（MinIO 存储）───────────────────
                {
                    try {
                        MysqlDao dao;
                        std::vector<FileTransferRecord> files;
                        if (dao.GetOfflineFiles(uid, files)) {
                            for (const auto& rec : files) {
                                // 重新生成一个新鲜 GET URL（防止存入DB时的URL已过期）
                                std::string fresh_url =
                                    MinioMgr::GetInstance()->GenerateGetUrl(rec.file_key);

                                Json::Value notif;
                                notif["sender_uid"]   = rec.sender_uid;
                                notif["file_key"]     = rec.file_key;
                                notif["filename"]     = rec.filename;
                                notif["size"]         = static_cast<Json::Int64>(rec.file_size);
                                notif["download_url"] = fresh_url;
                                notif["is_image"]     = rec.is_image;
                                session->Send(ID_FILE_MINIO_NOTIFY, notif.toStyledString());

                                dao.MarkFileDelivered(rec.file_key, uid);
                            }
                            if (!files.empty())
                                std::cout << "[AUTH] Delivered " << files.size()
                                          << " offline files to uid=" << uid << std::endl;
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[AUTH] Offline file delivery error: " << e.what() << std::endl;
                    }
                }
            } else {
                // 认证失败
                std::cout << "[AUTH] Token verification failed!" << std::endl;
                response["error"] = verify_rsp.error();
                response["msg"] = verify_rsp.msg();
                std::string response_str = response.toStyledString();
                std::cout << "[AUTH] Sending error response: " << response_str << std::endl;
                session->Send(ID_CHAT_LOGIN_RSP, response_str);
                std::cout << "[AUTH] Authentication failed: " << verify_rsp.msg() << std::endl;
            }
        });
    
    // 注册文本消息处理器
  // 注册文本消息处理器
RegisterHandler(ID_TEXT_CHAT_MSG,
    [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
        if (!session->IsAuthenticated()) {
            std::cout << "Unauthenticated message attempt" << std::endl;
            return;
        }

        // 解析 JSON
        Json::Value src_root;
        Json::Reader reader;
        if (!reader.parse(data, src_root)) {
            std::cout << "Text message: JSON parse error" << std::endl;
            Json::Value response;
            response["error"] = ErrorCodes::Error_Json;
            response["msg"] = "Invalid JSON format";
            session->Send(ID_TEXT_CHAT_MSG_RSP, response.toStyledString());
            return;
        }

        // sender uid 强类型校验（核心修复）
        int from_uid = 0;
        try {
            from_uid = std::stoi(session->GetUid());
        } catch (const std::exception& e) {
            std::cerr << "[TextMsg] Invalid sender uid in session: "
                      << session->GetUid() << ", err=" << e.what() << std::endl;
            Json::Value response;
            response["error"] = ErrorCodes::Error_Json;
            response["msg"] = "Invalid sender uid";
            session->Send(ID_TEXT_CHAT_MSG_RSP, response.toStyledString());
            return;
        }

        int to_uid = src_root["to_uid"].asInt();
        std::string content = src_root["content"].asString();
        if (to_uid <= 0) {
            Json::Value response;
            response["error"] = ErrorCodes::Error_Json;
            response["msg"] = "Invalid to_uid";
            session->Send(ID_TEXT_CHAT_MSG_RSP, response.toStyledString());
            return;
        }

        const std::string to_uid_str = std::to_string(to_uid);

        std::cout << "Chat message: " << from_uid << " -> " << to_uid
                  << ": " << content << std::endl;

        // 持久化聊天记录
        try {
            auto _sm = MysqlConPool::GetInstance()->GetSession();
            if (_sm) {
                MysqlObj obj_m(std::move(_sm));
                auto sch = obj_m.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                sch.getTable("chat_messages")
                   .insert("from_uid", "to_uid", "content")
                   .values(from_uid, to_uid, content)
                   .execute();
            }
        } catch (const std::exception& e) {
            std::cerr << "[TextMsg] Save chat_messages error: " << e.what() << std::endl;
        }

        // 离线落库
        auto save_offline_msg = [&]() {
            try {
                auto _sess2 = MysqlConPool::GetInstance()->GetSession();
                if (!_sess2) throw std::runtime_error("No DB session");
                MysqlObj obj2(std::move(_sess2));
                auto sch = obj2.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                sch.getTable("offline_messages")
                   .insert("from_uid", "to_uid", "content", "type")
                   .values(from_uid, to_uid, content, 0)
                   .execute();
                std::cout << "[TextMsg] Saved offline message for uid=" << to_uid << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[TextMsg] Save offline_message error: " << e.what() << std::endl;
            }
        };

        // 本机在线直推
        auto to_session = SessionManager::GetInstance()->GetSession(to_uid_str);
        if (to_session) {
            Json::Value forward_msg;
            forward_msg["from_uid"]  = from_uid;  // int，避免客户端 toInt() 变 0
            forward_msg["content"]   = content;
            forward_msg["timestamp"] = static_cast<Json::Int64>(time(nullptr));
            to_session->Send(ID_TEXT_CHAT_MSG, forward_msg.toStyledString());
            std::cout << "[TextMsg] Delivered locally to uid=" << to_uid << std::endl;
        } else {
            // Redis 查跨服
            std::string server_addr;
            bool on_other = RedisMgr::GetInstance()->Get("uid_server:" + to_uid_str, server_addr);

            if (on_other && !server_addr.empty()) {
                auto colon = server_addr.find(':');
                std::string tgt_host = server_addr.substr(0, colon);
                std::string tgt_port = server_addr.substr(colon + 1);

                Json::Value fwd;
                fwd["__ntype"]   = 3;
                fwd["content"]   = content;
                fwd["timestamp"] = static_cast<Json::Int64>(time(nullptr));

                auto deliver_rsp = ChatGrpcClient::GetInstance()->DeliverAddFriend(
                    tgt_host, tgt_port, from_uid, "", to_uid, fwd.toStyledString());

                if (deliver_rsp.error() == ErrorCodes::UserOffline) {
                    std::cout << "[TextMsg] uid=" << to_uid
                              << " went offline during cross-server forward, saving offline"
                              << std::endl;
                    save_offline_msg();
                } else {
                    std::cout << "[TextMsg] Cross-server delivered to uid=" << to_uid
                              << " via " << server_addr << std::endl;
                }
            } else {
                std::cout << "[TextMsg] uid=" << to_uid
                          << " offline, saving to offline_messages" << std::endl;
                save_offline_msg();
            }
        }

        // 回给发送方
        Json::Value response;
        response["error"] = ErrorCodes::Success;
        session->Send(ID_TEXT_CHAT_MSG_RSP, response.toStyledString());
    });
    
    // 注册心跳处理器
    RegisterHandler(ID_HEARTBEAT,
        [](std::shared_ptr<CSession> session, int msg_id, const std::string& data) {
            // 心跳响应
            Json::Value response;
            response["error"] = ErrorCodes::Success;
            response["msg"] = "pong";
            session->Send(ID_HEARTBEAT, response.toStyledString());
        });

    // ── 添加好友请求 ─────────────────────────────────────────────────────
    RegisterHandler(ID_ADD_FRIEND_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                std::cout << "[AddFriend] rejected: not authenticated" << std::endl;
                return;
            }

            Json::Value src;
            Json::Reader reader;
            if (!reader.parse(data, src)) {
                Json::Value rsp;
                rsp["error"] = ErrorCodes::Error_Json;
                session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            int         from_uid  = std::stoi(session->GetUid());
            std::string from_name = session->GetUser();
            int         to_uid    = src["to_uid"].asInt();
            std::string msg_text  = src["message"].asString();

            Json::Value rsp;

            // 0. 判重：已是好友 / 已有未读的待处理申请
            {
                MysqlDao dao_chk;
                if (dao_chk.AreFriends(from_uid, to_uid)) {
                    rsp["error"] = ErrorCodes::Success;
                    rsp["msg"]   = "Already friends";
                    session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
                    return;
                }
                try {
                    auto _s = MysqlConPool::GetInstance()->GetSession();
                    if (_s) {
                        MysqlObj obj_chk(std::move(_s));
                        auto sch = obj_chk.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                        auto res = sch.getTable("offline_messages")
                                      .select("id")
                                      .where("from_uid = :f AND to_uid = :t AND type = 1 AND delivered = 0")
                                      .bind("f", from_uid).bind("t", to_uid)
                                      .execute();
                        if (!res.fetchOne().isNull()) {
                            rsp["error"] = ErrorCodes::Success;
                            rsp["msg"]   = "Friend request already pending";
                            session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
                            return;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[AddFriend] dedup check error: " << e.what() << std::endl;
                }
            }

            // 1. 尝试本机直接推送
            auto to_session = SessionManager::GetInstance()->GetSession(std::to_string(to_uid));
            if (to_session) {
                Json::Value notif;
                notif["from_uid"]  = from_uid;
                notif["from_name"] = from_name;
                notif["message"]   = msg_text;
                to_session->Send(ID_NOTIFY_ADD_FRIEND, notif.toStyledString());
                std::cout << "[AddFriend] pushed locally uid=" << to_uid << std::endl;
                rsp["error"] = ErrorCodes::Success;
                rsp["msg"]   = "Friend request sent";
                session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            // ── 保存好友申请为离线消息的 lambda ─────────────────────────
            auto save_offline_friend_req = [&]() {
                try {
                    auto _s = MysqlConPool::GetInstance()->GetSession();
                    if (!_s) return;
                    MysqlObj g(std::move(_s));
                    // content = JSON 体，登录时直接转发给客户端
                    Json::Value payload;
                    payload["from_uid"]  = from_uid;
                    payload["from_name"] = from_name;
                    payload["message"]   = msg_text;
                    auto sch = g.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                    sch.getTable("offline_messages")
                       .insert("from_uid", "to_uid", "content", "type")
                       .values(from_uid, to_uid,
                               payload.toStyledString(), 1 /*好友申请*/)
                       .execute();
                    std::cout << "[AddFriend] Saved offline friend-req: "
                              << from_uid << " -> " << to_uid << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[AddFriend] save offline error: " << e.what() << std::endl;
                }
            };

            // 2. 用户不在本机 → 从 Redis 查询其所在的 ChatServer
            std::string server_addr;
            bool target_online = RedisMgr::GetInstance()->Get(
                "uid_server:" + std::to_string(to_uid), server_addr);
            if (!target_online || server_addr.empty()) {
                // 目标用户离线 → 存入离线消息，等下次登录投递
                save_offline_friend_req();
                rsp["error"] = ErrorCodes::Success;   // 对发送方返回成功，请求已入队
                rsp["msg"]   = "Friend request queued for offline delivery";
                session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
                return;
            }
            // 解析 host:grpc_port
            auto colon_pos = server_addr.find(':');
            std::string target_host = server_addr.substr(0, colon_pos);
            std::string target_port = server_addr.substr(colon_pos + 1);

            // 3. 通过 gRPC 转发给目标 ChatServer
            auto deliver_rsp = ChatGrpcClient::GetInstance()->DeliverAddFriend(
                target_host, target_port,
                from_uid, from_name, to_uid, msg_text);

            if (deliver_rsp.error() == ErrorCodes::UserOffline) {
                // 用户在 StatusServer 记录中存在但实际已断线（竞态）→ 落库兜底
                save_offline_friend_req();
                rsp["error"] = ErrorCodes::Success;
                rsp["msg"]   = "Friend request queued for offline delivery";
            } else {
                rsp["error"] = deliver_rsp.error();
                rsp["msg"]   = deliver_rsp.msg();
            }
            session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
            std::cout << "[AddFriend] cross-server deliver result="
                      << deliver_rsp.error() << std::endl;
        });

    // ── 同意好友申请 ──────────────────────────────────────────────────────
    // 客户端 JSON: { "peer_uid": <申请发起方 uid> }
    // 服务端逻辑:
    //   1. 向 chat_friends 写入双向好友关系
    //   2. 回复本机 accepter: ID_ACCEPT_FRIEND_RSP { error, friend_uid, friend_name }
    //   3. 推送给 requester:  ID_NOTIFY_ACCEPT_FRIEND { from_uid, from_name }
    RegisterHandler(ID_ACCEPT_FRIEND_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) return;

            Json::Value src;
            Json::Reader reader;
            if (!reader.parse(data, src)) {
                Json::Value rsp;
                rsp["error"] = ErrorCodes::Error_Json;
                session->Send(ID_ACCEPT_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            int accepter_uid  = std::stoi(session->GetUid());
            std::string accepter_name = session->GetUser();
            int requester_uid = src["peer_uid"].asInt();

            std::cout << "[AcceptFriend] accepter=" << accepter_uid
                      << " requester=" << requester_uid << std::endl;

            // 1. 写入好友关系表
            MysqlDao dao;
            bool ok = dao.AddFriendRelation(accepter_uid, requester_uid);

            Json::Value rsp;
            if (!ok) {
                rsp["error"] = ErrorCodes::RPCFailed;
                rsp["msg"]   = "Failed to write friend relation to database";
                session->Send(ID_ACCEPT_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            // 2. 获取 requester 的用户信息
            UserInfo requesterInfo;
            dao.GetUserByUid(requester_uid, requesterInfo);

            // 3. 回复 accepter
            rsp["error"]       = ErrorCodes::Success;
            rsp["friend_uid"]  = requester_uid;
            rsp["friend_name"] = requesterInfo.name;
            session->Send(ID_ACCEPT_FRIEND_RSP, rsp.toStyledString());
            std::cout << "[AcceptFriend] sent RSP to accepter uid=" << accepter_uid << std::endl;

            // 4. 通知 requester（先尝试本机直推）
            Json::Value notif;
            notif["from_uid"]  = accepter_uid;
            notif["from_name"] = accepter_name;

            auto req_session = SessionManager::GetInstance()->GetSession(
                                   std::to_string(requester_uid));
            if (req_session) {
                req_session->Send(ID_NOTIFY_ACCEPT_FRIEND, notif.toStyledString());
                std::cout << "[AcceptFriend] local push to requester uid=" << requester_uid << std::endl;
                return;
            }

            // ── 保存接受通知为离线消息的 lambda ─────────────────────────
            auto save_offline_accept = [&]() {
                try {
                    auto _s = MysqlConPool::GetInstance()->GetSession();
                    if (!_s) return;
                    MysqlObj g(std::move(_s));
                    Json::Value payload;
                    payload["from_uid"]  = accepter_uid;
                    payload["from_name"] = accepter_name;
                    auto sch = g.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                    sch.getTable("offline_messages")
                       .insert("from_uid", "to_uid", "content", "type")
                       .values(accepter_uid, requester_uid,
                               payload.toStyledString(), 2 /*好友已接受*/)
                       .execute();
                    std::cout << "[AcceptFriend] Saved offline accept-notify: "
                              << accepter_uid << " -> " << requester_uid << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[AcceptFriend] save offline error: " << e.what() << std::endl;
                }
            };

            // requester 不在本机 → 从 Redis 查询其所在 ChatServer 并跨服推送
            std::string req_server_addr;
            bool req_online = RedisMgr::GetInstance()->Get(
                "uid_server:" + std::to_string(requester_uid), req_server_addr);
            if (!req_online || req_server_addr.empty()) {
                // requester 已离线 → 存入离线消息，等其下次登录投递
                save_offline_accept();
                std::cout << "[AcceptFriend] requester uid=" << requester_uid
                          << " offline. Accept notification queued." << std::endl;
                return;
            }
            auto req_colon = req_server_addr.find(':');
            std::string req_host = req_server_addr.substr(0, req_colon);
            std::string req_port = req_server_addr.substr(req_colon + 1);

            // 将通知类型编码进 message 字段: {"__ntype":2,...} 供对端 ChatServiceImpl 区分
            notif["__ntype"] = 2;
            auto deliver_rsp = ChatGrpcClient::GetInstance()->DeliverAddFriend(
                req_host, req_port,
                accepter_uid, accepter_name,
                requester_uid, notif.toStyledString());

            if (deliver_rsp.error() == ErrorCodes::UserOffline) {
                // 竞态：用户刚断线 → 落库兜底
                save_offline_accept();
                std::cout << "[AcceptFriend] requester uid=" << requester_uid
                          << " went offline during delivery. Queued." << std::endl;
            } else {
                std::cout << "[AcceptFriend] cross-server accept notify result="
                          << deliver_rsp.error() << std::endl;
            }
        });

    // ════════════════════════════════════════════════════════════════════════
    // 旧版分块 TCP 文件传输已移除（改用 MinIO 对象存储，见下方）
    // ════════════════════════════════════════════════════════════════════════

    // ── ID_DELETE_FRIEND_REQ ─────────────────────────────────────────────
    RegisterHandler(ID_DELETE_FRIEND_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;  Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int self_uid   = std::stoi(session->GetUid());
            int friend_uid = src["friend_uid"].asInt();

            Json::Value rsp;
            rsp["friend_uid"] = friend_uid;

            MysqlDao dao;
            bool ok = dao.DeleteFriendRelation(self_uid, friend_uid);
            rsp["error"] = ok ? ErrorCodes::Success : ErrorCodes::RPCFailed;
            session->Send(ID_DELETE_FRIEND_RSP, rsp.toStyledString());
            std::cout << "[Friend] DeleteFriend: self=" << self_uid
                      << " friend=" << friend_uid << " ok=" << ok << std::endl;
        });

    // ── ID_BLOCK_FRIEND_REQ ──────────────────────────────────────────────
    RegisterHandler(ID_BLOCK_FRIEND_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;  Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int self_uid   = std::stoi(session->GetUid());
            int friend_uid = src["friend_uid"].asInt();

            Json::Value rsp;
            rsp["friend_uid"] = friend_uid;

            MysqlDao dao;
            bool ok = dao.BlockFriend(self_uid, friend_uid);
            rsp["error"] = ok ? ErrorCodes::Success : ErrorCodes::RPCFailed;
            session->Send(ID_BLOCK_FRIEND_RSP, rsp.toStyledString());
            std::cout << "[Friend] BlockFriend: blocker=" << self_uid
                      << " blocked=" << friend_uid << " ok=" << ok << std::endl;
        });

    // ── ID_UPDATE_PROFILE_REQ ────────────────────────────────────────────
    RegisterHandler(ID_UPDATE_PROFILE_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;  Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int         uid       = std::stoi(session->GetUid());
            std::string name      = src["name"].asString();
            std::string signature = src.get("signature", "").asString();
            std::string avatarB64 = src.get("avatar_b64", "").asString();
            std::string avatarExt = src.get("avatar_ext", "png").asString();

            Json::Value rsp;
            rsp["name"]      = name;
            rsp["signature"] = signature;

            // ── 如果有头像数据，解码并保存 ─────────────────────────────────
            std::string avatarPath;
            if (!avatarB64.empty()) {
                try {
                    auto decoded = b64_decode(avatarB64);
                    fs::path avatarDir = fs::path("avatars");
                    fs::create_directories(avatarDir);
                    fs::path filePath = avatarDir / (std::to_string(uid) + "." + avatarExt);
                    std::ofstream ofs(filePath, std::ios::binary);
                    ofs.write(reinterpret_cast<const char*>(decoded.data()), decoded.size());
                    ofs.close();
                    avatarPath = filePath.string();
                } catch (const std::exception& e) {
                    std::cerr << "[Profile] Avatar save error: " << e.what() << std::endl;
                }
            }

            MysqlDao dao;
            bool ok = dao.UpdateUserProfile(uid, name, signature, avatarPath);
            rsp["error"] = ok ? ErrorCodes::Success : ErrorCodes::RPCFailed;
            session->Send(ID_UPDATE_PROFILE_RSP, rsp.toStyledString());
            std::cout << "[Profile] UpdateProfile uid=" << uid
                      << " name=" << name << " ok=" << ok << std::endl;
        });

    // ════════════════════════════════════════════════════════════════════════
    // MinIO 对象存储文件传输
    // ════════════════════════════════════════════════════════════════════════

    // ── ID_FILE_MINIO_INIT_REQ ───────────────────────────────────────────
    // client → server: { receiver_uid, filename, size, is_image }
    // server → client: { file_key, upload_url }
    RegisterHandler(ID_FILE_MINIO_INIT_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;  Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int         sender_uid   = std::stoi(session->GetUid());
            std::string filename     = src["filename"].asString();

            // 生成唯一对象 key，作为 MinIO 中的对象路径
            std::string file_key = MinioMgr::MakeFileKey(sender_uid, filename);

            // 生成预签名 PUT URL（有效期 1 小时，足够客户端上传）
            std::string upload_url = MinioMgr::GetInstance()->GeneratePutUrl(file_key, 3600);

            Json::Value rsp;
            rsp["file_key"]   = file_key;
            rsp["upload_url"] = upload_url;
            session->Send(ID_FILE_MINIO_INIT_RSP, rsp.toStyledString());

            std::cout << "[MinIO] INIT_RSP: file_key=" << file_key
                      << " sender=" << sender_uid << std::endl;
        });

    // ── ID_FILE_MINIO_COMMIT ─────────────────────────────────────────────
    // client → server: 文件已直传 MinIO，请写 DB 并通知接收方
    // { file_key, filename, size, receiver_uid, is_image }
    RegisterHandler(ID_FILE_MINIO_COMMIT,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;  Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int         sender_uid   = std::stoi(session->GetUid());
            int         receiver_uid = src["receiver_uid"].asInt();
            std::string file_key     = src["file_key"].asString();
            std::string filename     = src["filename"].asString();
            int64_t     file_size    = src["size"].asInt64();
            bool        is_image     = src.get("is_image", false).asBool();

            // 生成供接收方使用的预签名 GET URL（7 天有效）
            std::string download_url =
                MinioMgr::GetInstance()->GenerateGetUrl(file_key, 604800);

            // 写入 DB（无论接收方是否在线，先持久化）
            MysqlDao dao;
            bool saved = dao.SaveFileTransfer(sender_uid, receiver_uid,
                                              file_key, filename, file_size,
                                              download_url, is_image);

            // 给发送方回执
            Json::Value rsp;
            rsp["error"]    = saved ? ErrorCodes::Success : ErrorCodes::RPCFailed;
            rsp["file_key"] = file_key;
            session->Send(ID_FILE_MINIO_COMMIT_RSP, rsp.toStyledString());

            if (!saved) {
                std::cerr << "[MinIO] Failed to save file_transfer record: "
                          << file_key << std::endl;
                return;
            }

            // 通知接收方（在线：立即推送；离线：等下次登录时由 DB 下发）
            auto recv_sess = SessionManager::GetInstance()->GetSession(
                                 std::to_string(receiver_uid));
            if (recv_sess) {
                Json::Value notif;
                notif["sender_uid"]   = sender_uid;
                notif["file_key"]     = file_key;
                notif["filename"]     = filename;
                notif["size"]         = static_cast<Json::Int64>(file_size);
                notif["download_url"] = download_url;
                notif["is_image"]     = is_image;
                recv_sess->Send(ID_FILE_MINIO_NOTIFY, notif.toStyledString());
                // 在线推送后立即标记为已投递
                dao.MarkFileDelivered(file_key, receiver_uid);
                std::cout << "[MinIO] Notified online receiver uid=" << receiver_uid << std::endl;
            } else {
                std::cout << "[MinIO] Receiver uid=" << receiver_uid
                          << " offline. Will deliver on next login." << std::endl;
            }
        });

    // ── ID_FILE_MINIO_REFRESH_REQ ────────────────────────────────────────
    // client → server: 预签名 URL 已过期，需要新 URL
    // { file_key }
    RegisterHandler(ID_FILE_MINIO_REFRESH_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;  Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            std::string file_key = src["file_key"].asString();
            std::string fresh_url = MinioMgr::GetInstance()->GenerateGetUrl(file_key, 604800);

            Json::Value rsp;
            rsp["file_key"]     = file_key;
            rsp["download_url"] = fresh_url;
            session->Send(ID_FILE_MINIO_REFRESH_RSP, rsp.toStyledString());

            std::cout << "[MinIO] Refreshed URL for key=" << file_key << std::endl;
        });
  RegisterHandler(ID_WEBRTC_SIGNALING, [](std::shared_ptr<CSession> session, int, const std::string& data) {
    if (!session->IsAuthenticated()) {
        std::cout << "[WebRTC] Unauthenticated signaling attempt" << std::endl;
        return;
    }

    // 1. 解析 JSON 请求
    Json::Value src_root;
    Json::Reader reader;
    if (!reader.parse(data, src_root)) {
        std::cout << "[WebRTC] JSON parse error" << std::endl;
        return;
    }

    int from_uid = src_root["from_uid"].asInt();
    int target_uid = src_root["to_uid"].asInt();
    std::string type = src_root["type"].asString();

    // 💥 修复核心：绝对不能调用 .asString() 去解析 payload，因为它是个 JSON 对象！
    // 我们保留它的 Json::Value 对象形态，供后续使用。
    Json::Value payload_obj = src_root["payload"];

    std::string target_uid_str = std::to_string(target_uid);
    std::cout << "[WebRTC] Signaling: " << type << " from " << from_uid << " to " << target_uid << std::endl;

    // 2. 尝试在本机 Session 管理器中查找目标
    auto to_session = SessionManager::GetInstance()->GetSession(target_uid_str);

    if (to_session) {
        // ── 情况 1：对方连接在当前服务器，直接 TCP 推送 ──
        Json::Value forward_msg;
        forward_msg["from_uid"] = from_uid;
        forward_msg["to_uid"] = target_uid;
        forward_msg["type"] = type;

        // 🟢 完美修复：直接把 JSON 对象赋值过去，JsonCpp 会自动处理嵌套！
        forward_msg["payload"] = payload_obj;

        // 发送给目标客户端
        to_session->Send(ID_WEBRTC_SIGNALING, forward_msg.toStyledString());
        std::cout << "[WebRTC] Delivered locally to uid=" << target_uid << std::endl;
    }
    else {
        // ── 情况 2：查询 Redis 确认对方是否在其他 ChatServer 上 ──
        std::string server_addr;
        bool on_other = false;
        try {
            on_other = RedisMgr::GetInstance()->Get("uid_server:" + target_uid_str, server_addr);
        } catch (...) {
            std::cerr << "[WebRTC] Redis 获取异常，目标状态未知" << std::endl;
        }

        if (on_other && !server_addr.empty()) {
            auto colon = server_addr.find(':');
            std::string tgt_host = server_addr.substr(0, colon);
            std::string tgt_port = server_addr.substr(colon + 1);

            // 🟢 完美修复：gRPC 接口通常需要 std::string，我们使用 FastWriter 安全地将 JSON 对象序列化成字符串
            Json::FastWriter writer;
            std::string payload_str = writer.write(payload_obj);

            auto deliver_rsp = ChatGrpcClient::GetInstance()->DeliverWebRTCSignaling(
                tgt_host, tgt_port, from_uid, target_uid, type, payload_str);

            std::cout << "[WebRTC] Cross-server forward to " << server_addr
                      << " result_code=" << deliver_rsp.error() << std::endl;
        }
        else {
            // ── 情况 3：对方彻底离线 ──
            std::cout << "[WebRTC] target_uid=" << target_uid << " is offline. Rejecting call." << std::endl;
            Json::Value response;
            // 注意：确保你的 ErrorCodes 枚举里有这个值，如果没有可以暂时写成比如 1006
            response["error"] = ErrorCodes::UserOffline;
            response["to_uid"] = target_uid;
            // 注意：这是回发给发送方(session)，告诉他对方离线了
            session->Send(ID_WEBRTC_SIGNALING, response.toStyledString());
        }
    }
});
}

void ChatLogicSystem::RegisterHandler(int msg_id, MsgHandler handler) {
    _handlers[msg_id] = handler;
}

bool ChatLogicSystem::HandleMessage(int msg_id, 
                                    std::shared_ptr<CSession> session,
                                    const std::string& json_str) {
    auto it = _handlers.find(msg_id);
    if (it == _handlers.end()) {
        std::cout << "No handler for msg_id: " << msg_id << std::endl;
        return false;
    }
    
    // 调用处理器
    try {
        it->second(session, msg_id, json_str);
        return true;
    } catch (const std::exception& e) {
        std::cout << "Handler exception: " << e.what() << std::endl;
        return false;
    }
}
