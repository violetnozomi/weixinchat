#include "ChatServiceImpl.h"
#include "SessionManager.h"
#include "CSession.h"
#include "MySqlPool.h"
#include "DbWriteQueue.h"
#include "SnowflakeID.h"
#include <iostream>
#include <ctime>
#include <json/json.h>

Status ChatServiceImpl::DeliverAddFriend(
    ServerContext* /*context*/,
    const DeliverAddFriendReq* request,
    DeliverAddFriendRsp* reply)
{
    int         from_uid  = request->from_uid();
    std::string from_name = request->from_name();
    int         to_uid    = request->to_uid();
    std::string msg_text  = request->message();

    std::cout << "[gRPC DeliverAddFriend] from_uid=" << from_uid
              << " to_uid=" << to_uid << " msg=" << msg_text << std::endl;

    // ── 判断通知类型："__ntype":2 → 好友接受通知；3 → 跨服单聊文本；4 → 跨服群文本 ────
    int ntype = 1;   // 1=add-friend notify, 2=accept-friend, 3=direct text, 4=group text
    Json::Value payload;
    Json::Reader reader;
    if (reader.parse(msg_text, payload) && payload.isMember("__ntype")) {
        ntype = payload["__ntype"].asInt();
    }

    auto session = SessionManager::GetInstance()->GetSession(std::to_string(to_uid));
    if (!session) {
        if (ntype == 4) {
            reply->set_error(ErrorCodes::UserOffline);
            reply->set_msg("Group target offline on this node");
            return Status::OK;
        }
        // 用户已从本机断线 → fire-and-forget 到 DbWriteQueue，gRPC 线程立即返回
        std::cout << "[DeliverAddFriend] uid=" << to_uid
                  << " offline on this node, queuing to offline_messages" << std::endl;
        DbWriteQueue::GetInstance()->PostWrite(
            [=]() {
                try {
                    auto _s = MysqlConPool::GetInstance()->GetSession();
                    if (!_s) return;
                    MysqlObj g(std::move(_s));
                    auto sch = g.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                    if (ntype == 3) {
                        // 跨服文本消息
                        std::string content = payload.isMember("content")
                                              ? payload["content"].asString() : "";
                        sch.getTable("offline_messages")
                           .insert("id", "from_uid", "to_uid", "content", "type")
                           .values(NewSnowflakeID(), from_uid, to_uid, content, 0).execute();
                    } else if (ntype == 2) {
                        // 好友接受通知
                        Json::Value p;
                        p["from_uid"]  = from_uid;
                        p["from_name"] = from_name;
                        sch.getTable("offline_messages")
                           .insert("id", "from_uid", "to_uid", "content", "type")
                           .values(NewSnowflakeID(), from_uid, to_uid, p.toStyledString(), 2).execute();
                    } else {
                        // 好友申请通知
                        Json::Value p;
                        p["from_uid"]  = from_uid;
                        p["from_name"] = from_name;
                        p["message"]   = msg_text;
                        sch.getTable("offline_messages")
                           .insert("id", "from_uid", "to_uid", "content", "type")
                           .values(NewSnowflakeID(), from_uid, to_uid, p.toStyledString(), 1).execute();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[DeliverAddFriend] offline save error: " << e.what() << std::endl;
                }
            });
        reply->set_error(ErrorCodes::Success);
        reply->set_msg("Queued offline");
        return Status::OK;
    }

    if (ntype == 2) {
        // 好友已接受通知
        Json::Value notif;
        notif["from_uid"]  = from_uid;
        notif["from_name"] = from_name;
        session->Send(ID_NOTIFY_ACCEPT_FRIEND, notif.toStyledString());
        std::cout << "[DeliverAddFriend] Accept notify pushed to uid=" << to_uid << std::endl;
    } else if (ntype == 3) {
        // 跨服转发的普通文本消息
        Json::Value notif;
        notif["from_uid"]  = from_uid;
        notif["content"]   = payload.isMember("content") ? payload["content"] : Json::Value("");
        notif["timestamp"] = payload.isMember("timestamp")
                               ? payload["timestamp"]
                               : static_cast<Json::Int64>(time(nullptr));
        session->Send(ID_TEXT_CHAT_MSG, notif.toStyledString());
        std::cout << "[DeliverAddFriend] TextMsg forwarded to uid=" << to_uid << std::endl;
    } else if (ntype == 4) {
        // 跨服转发的群文本消息
        Json::Value notif;
        notif["msg_id"] = payload.isMember("msg_id") ? payload["msg_id"] : Json::Value("");
        notif["group_id"] = payload.isMember("group_id") ? payload["group_id"] : Json::Value("");
        notif["sender_uid"] = from_uid;
        notif["msg_type"] = payload.get("msg_type", 0);
        notif["content"] = payload.isMember("content") ? payload["content"] : Json::Value("");
        notif["timestamp"] = payload.isMember("timestamp")
                               ? payload["timestamp"]
                               : static_cast<Json::Int64>(time(nullptr));
        session->Send(ID_GROUP_TEXT_MSG, notif.toStyledString());
        std::cout << "[DeliverAddFriend] GroupText forwarded to uid=" << to_uid << std::endl;
    } else {
        // 普通好友申请通知
        Json::Value notif;
        notif["from_uid"]  = from_uid;
        notif["from_name"] = from_name;
        notif["message"]   = msg_text;
        session->Send(ID_NOTIFY_ADD_FRIEND, notif.toStyledString());
        std::cout << "[DeliverAddFriend] AddFriend notify pushed to uid=" << to_uid << std::endl;
    }

    reply->set_error(ErrorCodes::Success);
    reply->set_msg("Delivered");
    return Status::OK;
}

Status ChatServiceImpl::DeliverWebRTCSignaling(
    ServerContext* /*context*/,
    const DeliverSignalingReq* request,
    DeliverSignalingRsp* reply)
{
    const int from_uid = request->from_uid();
    const int to_uid = request->to_uid();
    const std::string type = request->type();
    const std::string payload_text = request->payload();

    auto session = SessionManager::GetInstance()->GetSession(std::to_string(to_uid));
    if (!session) {
        reply->set_error(ErrorCodes::UserOffline);
        return Status::OK;
    }

    Json::Value forward_msg;
    forward_msg["from_uid"] = from_uid;
    forward_msg["to_uid"] = to_uid;
    forward_msg["type"] = type;

    Json::Value payload;
    Json::Reader reader;
    if (reader.parse(payload_text, payload)) {
        forward_msg["payload"] = payload;
    } else {
        forward_msg["payload"] = payload_text;
    }

    session->Send(ID_WEBRTC_SIGNALING, forward_msg.toStyledString());
    reply->set_error(ErrorCodes::Success);
    return Status::OK;
}
