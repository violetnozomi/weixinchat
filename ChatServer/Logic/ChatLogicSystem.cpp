//
// Created by EDPerson on 2026/2/13.
//

#include "ChatLogicSystem.h"
#include "AgentManager.h"
#include "CSession.h"
#include "SessionManager.h"
#include "StatusGrpcClient.h"
#include "ChatGrpcClient.h"
#include "RedisMgr.h"
#include "MySqlPool.h"
#include "MinioMgr.h"
#include "DbWriteQueue.h"
#include "DbReadQueue.h"
#include "ConfigMgr.h"
#include "SnowflakeID.h"
#include "SyncService.h"
#include "DTO/AuthDTO.h"
#include "DTO/FriendDTO.h"
#include "DTO/GroupDTO.h"
#include "DTO/SyncDTO.h"
#include "Service/GroupMemberService.h"
#include "Service/GroupMessageService.h"
#include "Service/GroupService.h"
#include <boost/asio/post.hpp>
#include "base64.h"
#include <iostream>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <set>
#include <algorithm>
#include <json/json.h>
namespace fs = std::filesystem;

namespace {

void SendJsonValue(const std::shared_ptr<CSession>& session,
                   int msg_id,
                   const Json::Value& value) {
    session->Send(msg_id, dto::json::Serialize(value));
}

bool TryGetSessionUid(const std::shared_ptr<CSession>& session, int& uid) {
    try {
        uid = std::stoi(session->GetUid());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ChatLogic] Invalid session uid: "
                  << session->GetUid() << ", err=" << e.what() << std::endl;
        return false;
    }
}

}  // namespace

ChatLogicSystem::ChatLogicSystem() {
    // 注册认证消息处理器
    RegisterHandler(ID_CHAT_LOGIN_REQ, 
        [](std::shared_ptr<CSession> session, int msg_id, const std::string& data) {
            Json::Value src_root;
            if (!dto::json::Parse(data, src_root)) {
                dto::ChatLoginRsp rsp;
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid JSON format";
                SendJsonValue(session, ID_CHAT_LOGIN_RSP, dto::ToJson(rsp));
                return;
            }

            dto::ChatLoginReq req;
            if (!dto::FromJson(src_root, req) || req.uid <= 0 ||
                req.token.empty() || req.user.empty()) {
                dto::ChatLoginRsp rsp;
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid login payload";
                SendJsonValue(session, ID_CHAT_LOGIN_RSP, dto::ToJson(rsp));
                return;
            }

            auto verify_rsp = StatusGrpcClient::GetInstance()->LoginVerify(req.token, req.uid);
            if (verify_rsp.error() == ErrorCodes::Success) {
                const int login_uid = req.uid;
                std::string uid_str = std::to_string(login_uid);
                auto old_session = SessionManager::GetInstance()->GetSession(uid_str);
                if (old_session && old_session != session) {
                    // 断线重连或异地登录，关闭旧 socket
                    old_session->Close();
                }
                // 认证成功：注册 session
                session->SetAuthenticated(uid_str, req.user);

                SessionManager::GetInstance()->AddSession(uid_str, session);

                // 立即发送认证响应（在任何可能阻塞的 Redis/DB 操作之前）
                dto::ChatLoginRsp login_rsp;
                login_rsp.error = ErrorCodes::Success;
                login_rsp.msg = "Authentication successful";
                SendJsonValue(session, ID_CHAT_LOGIN_RSP, dto::ToJson(login_rsp));

                // 在 Redis 记录本机地址，供跨服路由查询（Redis 不可用时跳过，不影响登录）
                {
                    auto& cfg = ConfigMgr::Inst();
                    std::string my_host      = cfg["ChatServer"]["Host"];
                    std::string my_grpc_port = cfg["ChatServer"]["GrpcPort"];
                    std::string server_val   = my_host + ":" + my_grpc_port;
                    std::string redis_key    = "uid_server:" + std::to_string(login_uid);
                    try {
                        RedisMgr::GetInstance()->SetEx(redis_key, server_val, 86400);
                    } catch (const std::exception& e) {
                        std::cerr << "[AUTH] Redis SetEx skipped (Redis unavailable): " << e.what() << std::endl;
                    }
                }

                // ── 异步推送好友列表（DbReadQueue → PostTask 续体回逻辑线程）────
                {
                    DbReadQueue::GetInstance()->PostRead(
                        [login_uid, weak_sess = std::weak_ptr<CSession>(session)]() {
                            // DB 线程：执行 SELECT
                            struct FriendRow { int uid; std::string name, email; };
                            std::vector<FriendRow> rows;
                            try {
                                MysqlDao dao;
                                std::vector<UserInfo> friends;
                                if (dao.GetFriendsByUid(login_uid, friends)) {
                                    for (auto& f : friends)
                                        rows.push_back({f.uid, f.name, f.email});
                                }
                            } catch (const std::exception& e) {
                                std::cerr << "[AUTH] GetFriendsByUid error: " << e.what() << std::endl;
                            }
                            // 续体：回逻辑线程组包并发送
                            ChatLogicSystem::GetInstance()->PostTask(
                                weak_sess,
                                [rows = std::move(rows), weak_sess]() {
                                    auto sess = weak_sess.lock();
                                    if (!sess) return;
                                    dto::FriendsListPush rsp;
                                    // 注入 AI 机器人
                                    dto::FriendListItem bot;
                                    bot.uid = AgentManager::BOT_UID;
                                    bot.name = "AI\u52a9\u624b";
                                    bot.signature = "\u6709\u95ee\u5fc5\u7b54\uff0c\u77e5\u9519\u5c31\u6539\uff08\u5927\u6982\uff09";
                                    rsp.friends.push_back(bot);
                                    for (auto& f : rows) {
                                        dto::FriendListItem item;
                                        item.uid = f.uid;
                                        item.name = f.name;
                                        item.email = f.email;
                                        rsp.friends.push_back(item);
                                    }
                                    SendJsonValue(sess, ID_FRIENDS_LIST_RSP, dto::ToJson(rsp));
                                });
                        });
                }

                // ── 异步下发离线消息（DbReadQueue → PostTask 续体）─────────────
                {
                    DbReadQueue::GetInstance()->PostRead(
                        [login_uid, weak_sess = std::weak_ptr<CSession>(session)]() {
                            // DB 线程：SELECT 未读离线消息
                            struct OfflineRow {
                                int from_uid;
                                int type;
                                int64_t id;
                                std::string content;
                            };
                            std::vector<OfflineRow> rows;
                            try {
                                auto _s = MysqlConPool::GetInstance()->GetSession();
                                if (_s) {
                                    MysqlObj obj(std::move(_s));
                                    auto tbl = obj.get()
                                                  ->getSchema(MysqlConPool::GetInstance()->GetSchema())
                                                  .getTable("offline_messages");
                                    auto result = tbl.select("from_uid", "content", "type", "id")
                                                     .where("to_uid = :uid AND delivered = 0")
                                                     .bind("uid", login_uid).execute();
                                    for (auto row = result.fetchOne(); !row.isNull();
                                             row = result.fetchOne()) {
                                        rows.push_back({
                                            static_cast<int>(row[0]),
                                            static_cast<int>(row[2]),
                                            row[3].get<int64_t>(),
                                            row[1].get<std::string>()
                                        });
                                    }
                                }
                            } catch (const std::exception& e) {
                                std::cerr << "[AUTH] Read offline_messages error: " << e.what() << std::endl;
                            }
                            // 续体：回逻辑线程发送，再 fire-and-forget 写操作
                            ChatLogicSystem::GetInstance()->PostTask(
                                weak_sess,
                                [rows = std::move(rows), login_uid, weak_sess]() {
                                    auto sess = weak_sess.lock();
                                    if (sess) {
                                        for (auto& r : rows) {
                                            if (r.type == 0) {
                                                Json::Value msg;
                                                msg["msg_id"]    = "off_" + std::to_string(r.id);
                                                msg["from_uid"]  = r.from_uid;
                                                msg["content"]   = r.content;
                                                msg["timestamp"] = static_cast<Json::Int64>(time(nullptr));
                                                sess->Send(ID_TEXT_CHAT_MSG, msg.toStyledString());
                                            } else if (r.type == 1) {
                                                Json::Value parsed; Json::Reader rdr;
                                                if (rdr.parse(r.content, parsed))
                                                    sess->Send(ID_NOTIFY_ADD_FRIEND, parsed.toStyledString());
                                            } else if (r.type == 2) {
                                                Json::Value parsed; Json::Reader rdr;
                                                if (rdr.parse(r.content, parsed))
                                                    sess->Send(ID_NOTIFY_ACCEPT_FRIEND, parsed.toStyledString());
                                            }
                                        }
                                    }
                                    // 标记已投递 + 清理7天旧消息
                                    DbWriteQueue::GetInstance()->PostWrite([login_uid]() {
                                        try {
                                            auto _s = MysqlConPool::GetInstance()->GetSession();
                                            if (!_s) return;
                                            MysqlObj obj(std::move(_s));
                                            auto sch = obj.get()->getSchema(
                                                MysqlConPool::GetInstance()->GetSchema());
                                            // 新 schema 新增 delivered_at 列，raw SQL 调用 NOW()
                                            try {
                                                obj.get()->sql(
                                                    "UPDATE offline_messages "
                                                    "SET delivered=1, delivered_at=NOW() "
                                                    "WHERE to_uid=? AND delivered=0"
                                                ).bind(login_uid).execute();
                                            } catch (const std::exception&) {
                                                obj.get()->sql(
                                                    "UPDATE offline_messages "
                                                    "SET delivered=1 "
                                                    "WHERE to_uid=? AND delivered=0"
                                                ).bind(login_uid).execute();
                                            }
                                            obj.get()->sql(
                                                "DELETE FROM offline_messages "
                                                "WHERE to_uid = ? "
                                                "AND created_at < DATE_SUB(NOW(), INTERVAL 7 DAY)"
                                            ).bind(login_uid).execute();
                                        } catch (const std::exception& e) {
                                            std::cerr << "[AUTH] offline msg mark/cleanup error: "
                                                      << e.what() << std::endl;
                                        }
                                    });
                                });
                        });
                }

                // ── 异步推送离线文件记录（DbReadQueue → PostTask 续体）────────
                {
                    DbReadQueue::GetInstance()->PostRead(
                        [login_uid, weak_sess = std::weak_ptr<CSession>(session)]() {
                            // DB 线程：查询未投递的文件记录
                            std::vector<FileTransferRecord> files;
                            try {
                                MysqlDao dao;
                                dao.GetOfflineFiles(login_uid, files);
                            } catch (const std::exception& e) {
                                std::cerr << "[AUTH] GetOfflineFiles error: " << e.what() << std::endl;
                            }
                            // 续体：回逻辑线程发送通知，再 fire-and-forget 标记已投递
                            ChatLogicSystem::GetInstance()->PostTask(
                                weak_sess,
                                [files = std::move(files), login_uid, weak_sess]() {
                                    auto sess = weak_sess.lock();
                                    for (auto& rec : files) {
                                        // 重新生成新鲜 GET URL（防旧 URL 过期）
                                        std::string fresh_url =
                                            MinioMgr::GetInstance()->GenerateGetUrl(rec.file_key);
                                        if (sess) {
                                            Json::Value notif;
                                            notif["sender_uid"]   = rec.sender_uid;
                                            notif["file_key"]     = rec.file_key;
                                            notif["filename"]     = rec.filename;
                                            notif["size"]         = static_cast<Json::Int64>(rec.file_size);
                                            notif["download_url"] = fresh_url;
                                            notif["is_image"]     = rec.is_image;
                                            notif["is_voice"]     = rec.is_voice;
                                            notif["duration_ms"]  = rec.duration_ms;
                                            sess->Send(ID_FILE_MINIO_NOTIFY, notif.toStyledString());
                                        }
                                        // 无论 session 是否还在线，均标记已投递（幂等）
                                        std::string fk = rec.file_key;
                                        DbWriteQueue::GetInstance()->PostWrite([login_uid, fk]() {
                                            try {
                                                MysqlDao dao;
                                                dao.MarkFileDelivered(fk, login_uid);
                                            } catch (const std::exception& e) {
                                                std::cerr << "[AUTH] MarkFileDelivered error: "
                                                          << e.what() << std::endl;
                                            }
                                        });
                                    }
                                });
                        });
                }
            } else {
                // 认证失败
                dto::ChatLoginRsp login_rsp;
                login_rsp.error = verify_rsp.error();
                login_rsp.msg = verify_rsp.msg();
                SendJsonValue(session, ID_CHAT_LOGIN_RSP, dto::ToJson(login_rsp));
            }
        });
    
    // 注册文本消息处理器
  // 注册文本消息处理器
RegisterHandler(ID_TEXT_CHAT_MSG,
    [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
        if (!session->IsAuthenticated()) {
            return;
        }

        // 解析 JSON
        Json::Value src_root;
        Json::Reader reader;
        if (!reader.parse(data, src_root)) {
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

        // ── AI 机器人拦截 ────────────────────────────────────────────────────
        // 若收件人是 UID=9999（机器人），转交 AgentManager 异步处理，不做常规转发。
        // 先给发送方返回成功 RSP，让客户端 UI 即时解除等待状态。
        if (to_uid == AgentManager::BOT_UID) {
            // 持久化用户发给机器人的消息（异步写，不阻塞逻辑线程）
            DbWriteQueue::GetInstance()->PostWrite([from_uid, to_uid, content]() {
                try {
                    auto _s = MysqlConPool::GetInstance()->GetSession();
                    if (!_s) return;
                    MysqlObj obj(std::move(_s));
                    obj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
                       .getTable("chat_messages")
                       .insert("id", "from_uid", "to_uid", "content")
                       .values(NewSnowflakeID(), from_uid, to_uid, content).execute();

                    int64_t timeline_msg_id = 0;
                    if (!SyncService::GetInstance()->saveMessage(
                            from_uid, to_uid, 0, content, timeline_msg_id)) {
                        std::cerr << "[TextMsg] Save bot timeline error" << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[TextMsg] Save bot chat_messages error: " << e.what() << std::endl;
                }
            });
            Json::Value bot_rsp;
            bot_rsp["error"] = ErrorCodes::Success;
            bot_rsp["msg"]   = "ok";
            session->Send(ID_TEXT_CHAT_MSG_RSP, bot_rsp.toStyledString());
            AgentManager::GetInstance()->onReceiveMessageFromUser(from_uid, content);
            return;
        }
        // ── 常规消息路径 ─────────────────────────────────────────────────────

        // 持久化聊天记录（异步写，不阻塞逻辑线程）
        DbWriteQueue::GetInstance()->PostWrite([from_uid, to_uid, content]() {
            try {
                auto _s = MysqlConPool::GetInstance()->GetSession();
                if (!_s) return;
                MysqlObj obj(std::move(_s));
                obj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
                   .getTable("chat_messages")
                   .insert("id", "from_uid", "to_uid", "content")
                   .values(NewSnowflakeID(), from_uid, to_uid, content)
                   .execute();

                int64_t timeline_msg_id = 0;
                if (!SyncService::GetInstance()->saveMessage(
                        from_uid, to_uid, 0, content, timeline_msg_id)) {
                    std::cerr << "[TextMsg] Save timeline error" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "[TextMsg] Save chat_messages error: " << e.what() << std::endl;
            }
        });

        // 离线落库（[=] 值捕获，可安全投递到 DbWriteQueue 异步执行）
        auto save_offline_msg = [=]() {
            try {
                auto _s = MysqlConPool::GetInstance()->GetSession();
                if (!_s) return;
                MysqlObj obj(std::move(_s));
                obj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
                   .getTable("offline_messages")
                   .insert("id", "from_uid", "to_uid", "content", "type")
                   .values(NewSnowflakeID(), from_uid, to_uid, content, 0)
                   .execute();
            } catch (const std::exception& e) {
                std::cerr << "[TextMsg] Save offline_message error: " << e.what() << std::endl;
            }
        };

        // 本机在线直推
        auto to_session = SessionManager::GetInstance()->GetSession(to_uid_str);
        if (to_session) {
            // P2-1: 唯一消息ID，供客户端去重（防重连重投）
            static std::atomic<uint64_t> s_msg_seq{1};
            std::string msg_id = std::to_string(from_uid) + "_"
                               + std::to_string(to_uid)   + "_"
                               + std::to_string(s_msg_seq.fetch_add(1));
            Json::Value forward_msg;
            forward_msg["msg_id"]    = msg_id;
            forward_msg["from_uid"]  = from_uid;  // int，避免客户端 toInt() 变 0
            forward_msg["content"]   = content;
            forward_msg["timestamp"] = static_cast<Json::Int64>(time(nullptr));
            to_session->Send(ID_TEXT_CHAT_MSG, forward_msg.toStyledString());
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
                    DbWriteQueue::GetInstance()->PostWrite(save_offline_msg);
                }
            } else {
                DbWriteQueue::GetInstance()->PostWrite(save_offline_msg);
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
    RegisterHandler(ID_SYNC_MSG_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            dto::SyncMsgRsp rsp;
            int owner_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, owner_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid sync request";
                SendJsonValue(session, ID_SYNC_MSG_RSP, dto::ToJson(rsp));
                return;
            }

            dto::SyncMsgReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid sync payload";
                SendJsonValue(session, ID_SYNC_MSG_RSP, dto::ToJson(rsp));
                return;
            }

            DbReadQueue::GetInstance()->PostRead(
                [owner_uid, last_sync_id = req.last_sync_id,
                 weak_sess = std::weak_ptr<CSession>(session)]() {
                    dto::SyncMsgRsp read_rsp;
                    try {
                        auto messages = SyncService::GetInstance()->pullSyncMessages(
                            owner_uid, last_sync_id);
                        read_rsp.latest_sync_id = last_sync_id;
                        read_rsp.messages.reserve(messages.size());
                        for (const auto& msg : messages) {
                            read_rsp.messages.push_back(dto::ToSyncMessageItem(msg));
                            read_rsp.latest_sync_id = std::max(
                                read_rsp.latest_sync_id,
                                static_cast<uint64_t>(msg.msg_id));
                        }
                    } catch (const std::exception& e) {
                        read_rsp.error = ErrorCodes::RPCFailed;
                        read_rsp.msg = "Failed to pull sync messages";
                        std::cerr << "[SyncMsg] pullSyncMessages error: "
                                  << e.what() << std::endl;
                    }

                    ChatLogicSystem::GetInstance()->PostTask(
                        weak_sess,
                        [weak_sess, read_rsp = std::move(read_rsp)]() mutable {
                            auto sess = weak_sess.lock();
                            if (!sess) return;
                            SendJsonValue(sess, ID_SYNC_MSG_RSP, dto::ToJson(read_rsp));
                        });
                });
        });

    RegisterHandler(ID_CREATE_GROUP_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            dto::CreateGroupRsp rsp;
            int owner_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, owner_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid create group request";
                SendJsonValue(session, ID_CREATE_GROUP_RSP, dto::ToJson(rsp));
                return;
            }

            dto::CreateGroupReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid create group payload";
                SendJsonValue(session, ID_CREATE_GROUP_RSP, dto::ToJson(rsp));
                return;
            }

            rsp = GroupService::GetInstance()->CreateGroup(owner_uid, req);
            SendJsonValue(session, ID_CREATE_GROUP_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_GROUP_INFO_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            dto::GroupInfoRsp rsp;
            int requester_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, requester_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid group info request";
                SendJsonValue(session, ID_GROUP_INFO_RSP, dto::ToJson(rsp));
                return;
            }

            dto::GroupInfoReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid group info payload";
                SendJsonValue(session, ID_GROUP_INFO_RSP, dto::ToJson(rsp));
                return;
            }

            rsp = GroupService::GetInstance()->GetGroupInfo(requester_uid, req);
            SendJsonValue(session, ID_GROUP_INFO_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_GROUP_MEMBER_LIST_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            dto::GroupMemberListRsp rsp;
            int requester_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, requester_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid group member list request";
                SendJsonValue(session, ID_GROUP_MEMBER_LIST_RSP, dto::ToJson(rsp));
                return;
            }

            dto::GroupMemberListReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid group member list payload";
                SendJsonValue(session, ID_GROUP_MEMBER_LIST_RSP, dto::ToJson(rsp));
                return;
            }

            rsp = GroupMemberService::GetInstance()->GetGroupMemberList(requester_uid, req);
            SendJsonValue(session, ID_GROUP_MEMBER_LIST_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_INVITE_GROUP_MEMBER_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            dto::InviteGroupMemberRsp rsp;
            int operator_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, operator_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid invite member request";
                SendJsonValue(session, ID_INVITE_GROUP_MEMBER_RSP, dto::ToJson(rsp));
                return;
            }

            dto::InviteGroupMemberReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid invite member payload";
                SendJsonValue(session, ID_INVITE_GROUP_MEMBER_RSP, dto::ToJson(rsp));
                return;
            }

            rsp = GroupMemberService::GetInstance()->InviteMembers(operator_uid, req);
            SendJsonValue(session, ID_INVITE_GROUP_MEMBER_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_QUIT_GROUP_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            dto::QuitGroupRsp rsp;
            int operator_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, operator_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid quit group request";
                SendJsonValue(session, ID_QUIT_GROUP_RSP, dto::ToJson(rsp));
                return;
            }

            dto::QuitGroupReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid quit group payload";
                SendJsonValue(session, ID_QUIT_GROUP_RSP, dto::ToJson(rsp));
                return;
            }

            rsp = GroupMemberService::GetInstance()->QuitGroup(operator_uid, req);
            SendJsonValue(session, ID_QUIT_GROUP_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_GROUP_TEXT_MSG,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            dto::GroupTextMsgRsp rsp;
            dto::GroupMessageItem pushed_msg;
            std::vector<int> member_uids;
            int sender_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, sender_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid group text request";
                SendJsonValue(session, ID_GROUP_TEXT_MSG_RSP, dto::ToJson(rsp));
                return;
            }

            dto::GroupTextMsgReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid group text payload";
                SendJsonValue(session, ID_GROUP_TEXT_MSG_RSP, dto::ToJson(rsp));
                return;
            }

            rsp = GroupMessageService::GetInstance()->SendTextMessage(
                sender_uid, req, &pushed_msg, &member_uids);
            SendJsonValue(session, ID_GROUP_TEXT_MSG_RSP, dto::ToJson(rsp));

            if (rsp.error != ErrorCodes::Success) {
                return;
            }

            Json::Value grpc_payload = dto::ToJson(pushed_msg);
            grpc_payload["__ntype"] = 4;
            const std::string grpc_payload_text = dto::json::Serialize(grpc_payload);

            for (int uid : member_uids) {
                if (uid == sender_uid) {
                    continue;
                }
                auto to_session = SessionManager::GetInstance()->GetSession(std::to_string(uid));
                if (!to_session) {
                    std::string server_addr;
                    bool on_other = false;
                    try {
                        on_other = RedisMgr::GetInstance()->Get(
                            "uid_server:" + std::to_string(uid), server_addr);
                    } catch (...) {
                        on_other = false;
                    }

                    if (!on_other || server_addr.empty()) {
                        continue;
                    }

                    auto colon = server_addr.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }

                    std::string tgt_host = server_addr.substr(0, colon);
                    std::string tgt_port = server_addr.substr(colon + 1);
                    auto deliver_rsp = ChatGrpcClient::GetInstance()->DeliverAddFriend(
                        tgt_host, tgt_port, sender_uid, "", uid, grpc_payload_text);
                    if (deliver_rsp.error() != ErrorCodes::Success &&
                        deliver_rsp.error() != ErrorCodes::UserOffline) {
                        std::cerr << "[GroupText] cross-server push failed, uid="
                                  << uid << " error=" << deliver_rsp.error() << std::endl;
                    }
                    continue;
                }
                SendJsonValue(to_session, ID_GROUP_TEXT_MSG, dto::ToJson(pushed_msg));
            }
        });

    RegisterHandler(ID_GROUP_SYNC_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            dto::GroupSyncRsp rsp;
            int owner_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, owner_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid group sync request";
                SendJsonValue(session, ID_GROUP_SYNC_RSP, dto::ToJson(rsp));
                return;
            }

            dto::GroupSyncReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid group sync payload";
                SendJsonValue(session, ID_GROUP_SYNC_RSP, dto::ToJson(rsp));
                return;
            }

            rsp = GroupMessageService::GetInstance()->PullSync(owner_uid, req);
            SendJsonValue(session, ID_GROUP_SYNC_RSP, dto::ToJson(rsp));
        });

    // --- Group admin operations ---

    RegisterHandler(ID_DISSOLVE_GROUP_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;
            dto::DissolveGroupRsp rsp;
            int operator_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, operator_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid dissolve group request";
                SendJsonValue(session, ID_DISSOLVE_GROUP_RSP, dto::ToJson(rsp));
                return;
            }
            dto::DissolveGroupReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid dissolve group payload";
                SendJsonValue(session, ID_DISSOLVE_GROUP_RSP, dto::ToJson(rsp));
                return;
            }
            rsp = GroupMemberService::GetInstance()->DissolveGroup(operator_uid, req);
            SendJsonValue(session, ID_DISSOLVE_GROUP_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_TRANSFER_OWNER_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;
            dto::TransferOwnerRsp rsp;
            int operator_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, operator_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid transfer owner request";
                SendJsonValue(session, ID_TRANSFER_OWNER_RSP, dto::ToJson(rsp));
                return;
            }
            dto::TransferOwnerReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid transfer owner payload";
                SendJsonValue(session, ID_TRANSFER_OWNER_RSP, dto::ToJson(rsp));
                return;
            }
            rsp = GroupMemberService::GetInstance()->TransferOwnership(operator_uid, req);
            SendJsonValue(session, ID_TRANSFER_OWNER_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_KICK_MEMBER_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;
            dto::KickMemberRsp rsp;
            int operator_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, operator_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid kick member request";
                SendJsonValue(session, ID_KICK_MEMBER_RSP, dto::ToJson(rsp));
                return;
            }
            dto::KickMemberReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid kick member payload";
                SendJsonValue(session, ID_KICK_MEMBER_RSP, dto::ToJson(rsp));
                return;
            }
            rsp = GroupMemberService::GetInstance()->KickMember(operator_uid, req);
            SendJsonValue(session, ID_KICK_MEMBER_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_SET_MUTE_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;
            dto::SetMuteRsp rsp;
            int operator_uid = 0;
            if (!dto::json::Parse(data, src) || !TryGetSessionUid(session, operator_uid)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid set mute request";
                SendJsonValue(session, ID_SET_MUTE_RSP, dto::ToJson(rsp));
                return;
            }
            dto::SetMuteReq req;
            if (!dto::FromJson(src, req)) {
                rsp.error = ErrorCodes::Error_Json;
                rsp.msg = "Invalid set mute payload";
                SendJsonValue(session, ID_SET_MUTE_RSP, dto::ToJson(rsp));
                return;
            }
            rsp = GroupMemberService::GetInstance()->SetMemberMute(operator_uid, req);
            SendJsonValue(session, ID_SET_MUTE_RSP, dto::ToJson(rsp));
        });

    RegisterHandler(ID_ADD_FRIEND_REQ,
        [](std::shared_ptr<CSession> session, int /*msg_id*/, const std::string& data) {
            if (!session->IsAuthenticated()) {
                return;
            }

            Json::Value src;
            Json::Value rsp;
            if (!dto::json::Parse(data, src)) {
                rsp["error"] = ErrorCodes::Error_Json;
                rsp["msg"] = "Invalid JSON format";
                session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            dto::AddFriendReq req;
            int from_uid = 0;
            if (!dto::FromJson(src, req) || req.to_uid <= 0 || !TryGetSessionUid(session, from_uid)) {
                rsp["error"] = ErrorCodes::Error_Json;
                rsp["msg"] = "Invalid add friend payload";
                session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            std::string from_name = session->GetUser();
            int to_uid = req.to_uid;
            std::string msg_text = req.message;

            // 0. 判重：已是好友 / 已有未读的待处理申请
            {
                MysqlDao dao_chk;
                if (dao_chk.AreFriends(from_uid, to_uid)) {
                    rsp["error"] = ErrorCodes::Success;
                    rsp["msg"] = "Already friends";
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
                            rsp["msg"] = "Friend request already pending";
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
                dto::NotifyAddFriend notif;
                notif.from_uid = from_uid;
                notif.from_name = from_name;
                notif.message = msg_text;
                SendJsonValue(to_session, ID_NOTIFY_ADD_FRIEND, dto::ToJson(notif));
                rsp["error"] = ErrorCodes::Success;
                rsp["msg"] = "Friend request sent";
                session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            // ── 保存好友申请为离线消息的 lambda ─────────────────────────
            auto save_offline_friend_req = [=]() {
                try {
                    auto _s = MysqlConPool::GetInstance()->GetSession();
                    if (!_s) return;
                    MysqlObj g(std::move(_s));
                    // content = JSON 体，登录时直接转发给客户端
                    Json::Value payload;
                    payload["from_uid"] = from_uid;
                    payload["from_name"] = from_name;
                    payload["message"] = msg_text;
                    auto sch = g.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                    sch.getTable("offline_messages")
                       .insert("id", "from_uid", "to_uid", "content", "type")
                       .values(NewSnowflakeID(), from_uid, to_uid,
                               payload.toStyledString(), 1 /*好友申请*/)
                       .execute();
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
                DbWriteQueue::GetInstance()->PostWrite(save_offline_friend_req);
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
                DbWriteQueue::GetInstance()->PostWrite(save_offline_friend_req);
                rsp["error"] = ErrorCodes::Success;
                rsp["msg"]   = "Friend request queued for offline delivery";
            } else {
                rsp["error"] = deliver_rsp.error();
                rsp["msg"]   = deliver_rsp.msg();
            }
            session->Send(ID_ADD_FRIEND_RSP, rsp.toStyledString());
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
            if (!dto::json::Parse(data, src)) {
                Json::Value rsp;
                rsp["error"] = ErrorCodes::Error_Json;
                rsp["msg"] = "Invalid JSON format";
                session->Send(ID_ACCEPT_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            dto::AcceptFriendReq req;
            int accepter_uid = 0;
            if (!dto::FromJson(src, req) || req.peer_uid <= 0 || !TryGetSessionUid(session, accepter_uid)) {
                Json::Value rsp;
                rsp["error"] = ErrorCodes::Error_Json;
                rsp["msg"] = "Invalid accept friend payload";
                session->Send(ID_ACCEPT_FRIEND_RSP, rsp.toStyledString());
                return;
            }

            std::string accepter_name = session->GetUser();
            int requester_uid = req.peer_uid;

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

            // 4. 通知 requester（先尝试本机直推）
            dto::NotifyAcceptFriend notif;
            notif.from_uid = accepter_uid;
            notif.from_name = accepter_name;

            auto req_session = SessionManager::GetInstance()->GetSession(
                                   std::to_string(requester_uid));
            if (req_session) {
                SendJsonValue(req_session, ID_NOTIFY_ACCEPT_FRIEND, dto::ToJson(notif));
                return;
            }

            // ── 保存接受通知为离线消息的 lambda ─────────────────────────
            auto save_offline_accept = [=]() {
                try {
                    auto _s = MysqlConPool::GetInstance()->GetSession();
                    if (!_s) return;
                    MysqlObj g(std::move(_s));
                    Json::Value payload;
                    payload["from_uid"]  = accepter_uid;
                    payload["from_name"] = accepter_name;
                    auto sch = g.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                    sch.getTable("offline_messages")
                       .insert("id", "from_uid", "to_uid", "content", "type")
                       .values(NewSnowflakeID(), accepter_uid, requester_uid,
                               payload.toStyledString(), 2 /*好友已接受*/)
                       .execute();
                } catch (const std::exception& e) {
                    std::cerr << "[AcceptFriend] save offline error: " << e.what() << std::endl;
                }
            };

            // requester 不在本机 → 从 Redis 查询其所在 ChatServer 并跨服推送
            std::string req_server_addr;
            bool req_online = RedisMgr::GetInstance()->Get(
                "uid_server:" + std::to_string(requester_uid), req_server_addr);
            if (!req_online || req_server_addr.empty()) {
                DbWriteQueue::GetInstance()->PostWrite(save_offline_accept);
                return;
            }
            auto req_colon = req_server_addr.find(':');
            std::string req_host = req_server_addr.substr(0, req_colon);
            std::string req_port = req_server_addr.substr(req_colon + 1);

            // 将通知类型编码进 message 字段: {"__ntype":2,...} 供对端 ChatServiceImpl 区分
            notif.ntype = 2;
            auto deliver_rsp = ChatGrpcClient::GetInstance()->DeliverAddFriend(
                req_host, req_port,
                accepter_uid, accepter_name,
                requester_uid, dto::json::Serialize(dto::ToJson(notif)));

            if (deliver_rsp.error() == ErrorCodes::UserOffline) {
                // 竞态：用户刚断线 → 落库兜底
                DbWriteQueue::GetInstance()->PostWrite(save_offline_accept);
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
        });

    // ── ID_UPDATE_PROFILE_REQ ────────────────────────────────────────────
    RegisterHandler(ID_UPDATE_PROFILE_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;  Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int uid = std::stoi(session->GetUid());
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
        });

    // ════════════════════════════════════════════════════════════════════════
    // MinIO 对象存储文件传输
    // ════════════════════════════════════════════════════════════════════════

    // ── ID_FILE_MINIO_INIT_REQ ───────────────────────────────────────────
    // client → server: { receiver_uid, filename, size, is_image, client_upload_id }
    // server → client: { file_key, upload_url, client_upload_id }
    RegisterHandler(ID_FILE_MINIO_INIT_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src;  Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int sender_uid   = std::stoi(session->GetUid());
            std::string filename     = src["filename"].asString();

            // 生成唯一对象 key，作为 MinIO 中的对象路径
            std::string file_key = MinioMgr::MakeFileKey(sender_uid, filename);

            // 生成预签名 PUT URL（有效期 1 小时，足够客户端上传）
            std::string upload_url = MinioMgr::GetInstance()->GeneratePutUrl(file_key, 3600);

            Json::Value rsp;
            rsp["file_key"]   = file_key;
            rsp["upload_url"] = upload_url;
            // P2: echo back client_upload_id for deterministic concurrent upload matching
            if (src.isMember("client_upload_id") && !src["client_upload_id"].asString().empty()) {
                rsp["client_upload_id"] = src["client_upload_id"].asString();
            }
            session->Send(ID_FILE_MINIO_INIT_RSP, rsp.toStyledString());
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
            bool        is_voice     = src.get("is_voice", false).asBool();
            int         duration_ms  = src.get("duration_ms", 0).asInt();

            // 生成供接收方使用的预签名 GET URL（7 天有效）
            std::string download_url =
                MinioMgr::GetInstance()->GenerateGetUrl(file_key, 604800);

            // 写入 DB（无论接收方是否在线，先持久化）
            MysqlDao dao;
            bool saved = dao.SaveFileTransfer(sender_uid, receiver_uid, is_voice, duration_ms,
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
                notif["is_voice"]     = is_voice;
                notif["duration_ms"]  = duration_ms;
                recv_sess->Send(ID_FILE_MINIO_NOTIFY, notif.toStyledString());
                // 在线推送后立即标记为已投递
                dao.MarkFileDelivered(file_key, receiver_uid);
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
        });

    // ══ ID_FILE_MINIO_MP_INIT_REQ ───────────────────────────────────────────────
    // client → server: { receiver_uid, filename, size, total_parts, part_size,
    //                       is_image, is_voice, duration_ms, client_upload_id }
    // server: CreateMultipartUpload 异步调用，生成所有分片预签名 URL 后回客户端
    RegisterHandler(ID_FILE_MINIO_MP_INIT_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src; Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int         sender_uid       = std::stoi(session->GetUid());
            std::string filename         = src["filename"].asString();
            int         total_parts      = src["total_parts"].asInt();
            int         part_size        = src.get("part_size", 8388608).asInt();
            std::string client_upload_id = src.get("client_upload_id", "").asString();

            std::string file_key = MinioMgr::MakeFileKey(sender_uid, filename);

            DbReadQueue::GetInstance()->PostRead(
                [file_key, total_parts, part_size, client_upload_id,
                 weak_sess = std::weak_ptr<CSession>(session)]() {

                    std::string upload_id =
                        MinioMgr::GetInstance()->CreateMultipartUpload(file_key);

                    ChatLogicSystem::GetInstance()->PostTask(
                        weak_sess,
                        [file_key, upload_id, total_parts, part_size,
                         client_upload_id, weak_sess]() {
                            auto sess = weak_sess.lock();
                            if (!sess) return;

                            Json::Value rsp;
                            if (upload_id.empty()) {
                                rsp["error"] = ErrorCodes::RPCFailed;
                                rsp["msg"]   = "Failed to initiate multipart upload on MinIO";
                                sess->Send(ID_FILE_MINIO_MP_INIT_RSP, rsp.toStyledString());
                                return;
                            }

                            // 生成所有分片预签名 URL
                            Json::Value part_urls(Json::objectValue);
                            for (int i = 1; i <= total_parts; ++i) {
                                part_urls[std::to_string(i)] =
                                    MinioMgr::GetInstance()->GeneratePartPutUrl(
                                        file_key, upload_id, i, 3600);
                            }

                            rsp["file_key"]   = file_key;
                            rsp["upload_id"]  = upload_id;
                            rsp["part_size"]  = part_size;
                            rsp["total_parts"]= total_parts;
                            rsp["part_urls"]  = part_urls;
                            if (!client_upload_id.empty())
                                rsp["client_upload_id"] = client_upload_id;
                            sess->Send(ID_FILE_MINIO_MP_INIT_RSP, rsp.toStyledString());
                        });
                });
        });

    // ══ ID_FILE_MINIO_MP_COMPLETE_REQ ─────────────────────────────────────────
    // client → server: { file_key, upload_id, parts:[{part_number,etag}],
    //                       receiver_uid, filename, size, is_image, is_voice, duration_ms }
    RegisterHandler(ID_FILE_MINIO_MP_COMPLETE_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src; Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            int         sender_uid   = std::stoi(session->GetUid());
            int         receiver_uid = src["receiver_uid"].asInt();
            std::string file_key     = src["file_key"].asString();
            std::string upload_id    = src["upload_id"].asString();
            std::string filename     = src["filename"].asString();
            int64_t     file_size    = src["size"].asInt64();
            bool        is_image     = src.get("is_image",   false).asBool();
            bool        is_voice     = src.get("is_voice",   false).asBool();
            int         duration_ms  = src.get("duration_ms", 0).asInt();

            std::vector<MinioMgr::PartInfo> parts;
            for (const auto& p : src["parts"]) {
                MinioMgr::PartInfo pi;
                pi.part_number = p["part_number"].asInt();
                pi.etag        = p["etag"].asString();
                parts.push_back(pi);
            }
            // S3 要求分片必须按 part_number 升序排列
            std::sort(parts.begin(), parts.end(),
                      [](const auto& a, const auto& b) {
                          return a.part_number < b.part_number; });

            DbReadQueue::GetInstance()->PostRead(
                [file_key, upload_id, parts,
                 sender_uid, receiver_uid, filename, file_size,
                 is_image, is_voice, duration_ms,
                 weak_sess = std::weak_ptr<CSession>(session)]() {

                    bool ok = MinioMgr::GetInstance()->CompleteMultipartUpload(
                        file_key, upload_id, parts);

                    ChatLogicSystem::GetInstance()->PostTask(
                        weak_sess,
                        [ok, file_key, sender_uid, receiver_uid, filename,
                         file_size, is_image, is_voice, duration_ms,
                         weak_sess]() {

                            // 回复发送方
                            if (auto sess = weak_sess.lock()) {
                                Json::Value rsp;
                                rsp["error"]    = ok ? (int)ErrorCodes::Success
                                                      : (int)ErrorCodes::RPCFailed;
                                rsp["file_key"] = file_key;
                                sess->Send(ID_FILE_MINIO_MP_COMPLETE_RSP,
                                           rsp.toStyledString());
                            }
                            if (!ok) return;

                            // 异步写 DB
                            std::string dl_url = MinioMgr::GetInstance()
                                ->GenerateGetUrl(file_key, 604800);
                            DbWriteQueue::GetInstance()->PostWrite(
                                [=]() {
                                    MysqlDao dao;
                                    dao.SaveFileTransfer(sender_uid, receiver_uid,
                                        is_voice, duration_ms, file_key, filename,
                                        file_size, dl_url, is_image);
                                });

                            // 通知接收方
                            auto recv = SessionManager::GetInstance()->GetSession(
                                std::to_string(receiver_uid));
                            if (recv) {
                                Json::Value notif;
                                notif["sender_uid"]   = sender_uid;
                                notif["file_key"]     = file_key;
                                notif["filename"]     = filename;
                                notif["size"]         = static_cast<Json::Int64>(file_size);
                                notif["download_url"] = dl_url;
                                notif["is_image"]     = is_image;
                                notif["is_voice"]     = is_voice;
                                notif["duration_ms"]  = duration_ms;
                                recv->Send(ID_FILE_MINIO_NOTIFY, notif.toStyledString());
                                DbWriteQueue::GetInstance()->PostWrite([=]() {
                                    MysqlDao dao;
                                    dao.MarkFileDelivered(file_key, receiver_uid);
                                });
                            }
                        });
                });
        });

    // ══ ID_FILE_MINIO_MP_RESUME_REQ ──────────────────────────────────────────
    // client → server: { file_key, upload_id, total_parts }
    // server: ListParts 查询已上传分片，生成剩余分片 URL 后回客户端
    RegisterHandler(ID_FILE_MINIO_MP_RESUME_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src; Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            std::string file_key  = src["file_key"].asString();
            std::string upload_id = src["upload_id"].asString();
            int total_parts       = src["total_parts"].asInt();

            DbReadQueue::GetInstance()->PostRead(
                [file_key, upload_id, total_parts,
                 weak_sess = std::weak_ptr<CSession>(session)]() {

                    auto done = MinioMgr::GetInstance()->ListParts(file_key, upload_id);

                    std::set<int> done_set;
                    for (auto& p : done) done_set.insert(p.part_number);

                    // 生成剩余分片预签名 URL
                    Json::Value remaining_urls(Json::objectValue);
                    for (int i = 1; i <= total_parts; ++i) {
                        if (!done_set.count(i)) {
                            remaining_urls[std::to_string(i)] =
                                MinioMgr::GetInstance()->GeneratePartPutUrl(
                                    file_key, upload_id, i, 3600);
                        }
                    }

                    ChatLogicSystem::GetInstance()->PostTask(
                        weak_sess,
                        [file_key, upload_id, done, total_parts,
                         remaining_urls, weak_sess]() {
                            auto sess = weak_sess.lock();
                            if (!sess) return;

                            Json::Value rsp;
                            rsp["file_key"]   = file_key;
                            rsp["upload_id"]  = upload_id;
                            rsp["total_parts"]= total_parts;

                            Json::Value dp_arr(Json::arrayValue);
                            for (auto& p : done) {
                                Json::Value ep;
                                ep["part_number"] = p.part_number;
                                ep["etag"]        = p.etag;
                                dp_arr.append(ep);
                            }
                            rsp["done_parts"]          = dp_arr;
                            rsp["remaining_part_urls"] = remaining_urls;
                            sess->Send(ID_FILE_MINIO_MP_RESUME_RSP, rsp.toStyledString());
                        });
                });
        });

    // ══ ID_FILE_MINIO_MP_ABORT_REQ ───────────────────────────────────────────
    // client → server: { file_key, upload_id }  fire-and-forget
    RegisterHandler(ID_FILE_MINIO_MP_ABORT_REQ,
        [](std::shared_ptr<CSession> session, int, const std::string& data) {
            if (!session->IsAuthenticated()) return;
            Json::Value src; Json::Reader rdr;
            if (!rdr.parse(data, src)) return;

            std::string file_key  = src["file_key"].asString();
            std::string upload_id = src["upload_id"].asString();

            // 不需要回执，上传失败的分段对象在 MinIO 中会根据 lifecycle 设置自动清理
            DbWriteQueue::GetInstance()->PostWrite([file_key, upload_id]() {
                MinioMgr::GetInstance()->AbortMultipartUpload(file_key, upload_id);
            });
        });

  RegisterHandler(ID_WEBRTC_SIGNALING, [](std::shared_ptr<CSession> session, int, const std::string& data) {
    if (!session->IsAuthenticated()) {
        return;
    }

    // 1. 解析 JSON 请求
    Json::Value src_root;
    Json::Reader reader;
    if (!reader.parse(data, src_root)) {
        return;
    }

    int from_uid = 0;
    if (!TryGetSessionUid(session, from_uid)) {
        return;
    }

    int target_uid = src_root["to_uid"].asInt();
    std::string type = src_root["type"].asString();
    if (src_root.isMember("from_uid")) {
        int client_from_uid = src_root["from_uid"].asInt();
        if (client_from_uid != 0 && client_from_uid != from_uid) {
            std::cerr << "[WebRTC] Ignored forged from_uid="
                      << client_from_uid
                      << ", using session uid=" << from_uid << std::endl;
        }
    }

    // 💥 修复核心：绝对不能调用 .asString() 去解析 payload，因为它是个 JSON 对象！
    // 我们保留它的 Json::Value 对象形态，供后续使用。
    Json::Value payload_obj = src_root["payload"];

    std::string target_uid_str = std::to_string(target_uid);

    // 2. 尝试在本机 Session 管理器中查找目标
    auto to_session = SessionManager::GetInstance()->GetSession(target_uid_str);

    if (to_session) {
        // ── 情况 1：对方连接在当前服务器，直接 TCP 推送 ──
        Json::Value forward_msg;
        forward_msg["from_uid"] = from_uid;
        forward_msg["to_uid"] = target_uid;
        forward_msg["type"] = type;
        forward_msg["payload"] = payload_obj;
        to_session->Send(ID_WEBRTC_SIGNALING, forward_msg.toStyledString());
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
        }
        else {
            // ── 情况 3：对方彻底离线 ──
            Json::Value response;
            // 注意：确保你的 ErrorCodes 枚举里有这个值，如果没有可以暂时写成比如 1006
            response["error"] = ErrorCodes::UserOffline;
            response["to_uid"] = target_uid;
            // 注意：这是回发给发送方(session)，告诉他对方离线了
            session->Send(ID_WEBRTC_SIGNALING, response.toStyledString());
        }
    }
});

    // 逻辑线程池 _logic_pool 已在成员初始化时启动（hardware_concurrency 个线程）。
}

void ChatLogicSystem::RegisterHandler(int msg_id, MsgHandler handler) {
    _handlers[msg_id] = handler;
}

ChatLogicSystem::~ChatLogicSystem() {
    Stop();
}

// 返回逻辑线程池的 Executor（类型擦除为 any_io_executor）。
// CSession 将用此创建 strand，实现对网络层完全透明。
boost::asio::any_io_executor ChatLogicSystem::GetLogicExecutor() {
    return _logic_pool.get_executor();
}

TextRouteResult ChatLogicSystem::RouteTextMessage(int from_uid,
                                                  int to_uid,
                                                  const std::string& content,
                                                  std::time_t timestamp) {
    TextRouteResult result;
    if (from_uid <= 0 || to_uid <= 0 || content.empty()) {
        result.error = ErrorCodes::InvalidRequest;
        return result;
    }

    const std::time_t send_time = timestamp > 0 ? timestamp : std::time(nullptr);
    const std::string to_uid_str = std::to_string(to_uid);

    auto save_offline_msg = [from_uid, to_uid, content]() {
        try {
            auto _s = MysqlConPool::GetInstance()->GetSession();
            if (!_s) return;
            MysqlObj obj(std::move(_s));
            obj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
               .getTable("offline_messages")
               .insert("id", "from_uid", "to_uid", "content", "type")
               .values(NewSnowflakeID(), from_uid, to_uid, content, 0)
               .execute();
        } catch (const std::exception& e) {
            std::cerr << "[TextRoute] Save offline_message error: "
                      << e.what() << std::endl;
        }
    };

    auto to_session = SessionManager::GetInstance()->GetSession(to_uid_str);
    if (to_session) {
        static std::atomic<uint64_t> s_msg_seq{1};
        Json::Value forward_msg;
        forward_msg["msg_id"] = std::to_string(from_uid) + "_" +
                                std::to_string(to_uid) + "_" +
                                std::to_string(s_msg_seq.fetch_add(1));
        forward_msg["from_uid"] = from_uid;
        forward_msg["content"] = content;
        forward_msg["timestamp"] = static_cast<Json::Int64>(send_time);
        to_session->Send(ID_TEXT_CHAT_MSG, forward_msg.toStyledString());
        result.delivered_live = true;
        return result;
    }

    std::string server_addr;
    bool on_other = false;
    try {
        on_other = RedisMgr::GetInstance()->Get("uid_server:" + to_uid_str, server_addr);
    } catch (...) {
        on_other = false;
    }

    if (on_other && !server_addr.empty()) {
        auto colon = server_addr.find(':');
        if (colon != std::string::npos) {
            std::string tgt_host = server_addr.substr(0, colon);
            std::string tgt_port = server_addr.substr(colon + 1);

            Json::Value fwd;
            fwd["__ntype"] = 3;
            fwd["content"] = content;
            fwd["timestamp"] = static_cast<Json::Int64>(send_time);

            auto deliver_rsp = ChatGrpcClient::GetInstance()->DeliverAddFriend(
                tgt_host, tgt_port, from_uid, "", to_uid, fwd.toStyledString());

            if (deliver_rsp.error() == ErrorCodes::Success) {
                result.delivered_live = true;
                return result;
            }

            if (deliver_rsp.error() == ErrorCodes::UserOffline) {
                DbWriteQueue::GetInstance()->PostWrite(save_offline_msg);
                result.queued_offline = true;
                return result;
            }

            result.error = deliver_rsp.error();
            return result;
        }
    }

    DbWriteQueue::GetInstance()->PostWrite(save_offline_msg);
    result.queued_offline = true;
    return result;
}

// 优雅停机：等待逻辑线程池中所有已在运行的任务完成后返回。
void ChatLogicSystem::Stop() {
    _logic_pool.stop();
    _logic_pool.join();
}

// ─── 网络线程入口：将消息包装为 lambda 后入队，立即返回 ──────────────────────
void ChatLogicSystem::PostMessage(int msg_id,
                                   std::shared_ptr<CSession> session,
                                   const std::string& body) {
    boost::asio::post(session->GetStrand(),
        [this, msg_id,
         weak_sess = std::weak_ptr<CSession>(session), body]() {
            auto sess = weak_sess.lock();
            if (!sess) return;
            DispatchMessage(sess, msg_id, body);
        });
}

// ─── DB 线程续体入口：路由到目标 Session 的 strand ─────────────────────────────
// 同 Session 的所有续体与业务消息共享同一 strand，天然串行，无需加锁。
// Session 已离线时降级到逻辑线程池（far from网络 io_context，彻底解耦）。
void ChatLogicSystem::PostTask(std::weak_ptr<CSession> routing,
                               std::function<void()> task) {
    auto sess = routing.lock();
    if (sess) {
        boost::asio::post(sess->GetStrand(), std::move(task));
    } else {
        // Session 已离线：降级到逻辑线程池（远离网络 io_context）
        boost::asio::post(_logic_pool, std::move(task));
    }
}

// ─── 派发 handler（在 strand lambda 中直接调用）──────────────────────────────────
void ChatLogicSystem::DispatchMessage(std::shared_ptr<CSession> session,
                                      int msg_id, const std::string& body) {
    auto it = _handlers.find(msg_id);
    if (it == _handlers.end()) {
        std::cerr << "[ChatLogic] No handler for msg_id: " << msg_id << std::endl;
        return;
    }
    try {
        it->second(session, msg_id, body);
    } catch (const std::exception& e) {
        std::cerr << "[ChatLogic] Handler exception (msg_id=" << msg_id << "): " << e.what() << std::endl;
    }
}
