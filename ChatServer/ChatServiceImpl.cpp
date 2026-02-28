#include "ChatServiceImpl.h"
#include "SessionManager.h"
#include "CSession.h"
#include "MySqlPool.h"
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

    // ── 判断通知类型："__ntype":2 → 好友接受通知；默认 → 好友申请通知 ────
    int ntype = 1;   // 1 = add-friend notify, 2 = accept-friend notify
    Json::Value payload;
    Json::Reader reader;
    if (reader.parse(msg_text, payload) && payload.isMember("__ntype")) {
        ntype = payload["__ntype"].asInt();
    }

    auto session = SessionManager::GetInstance()->GetSession(std::to_string(to_uid));
    if (!session) {
        // 用户已从本机断线 → 直接落库，调用方无需重复处理竞态
        std::cout << "[DeliverAddFriend] uid=" << to_uid
                  << " offline on this node, saving to offline_messages" << std::endl;
        try {
            auto _s = MysqlConPool::GetInstance()->GetSession();
            if (_s) {
                MysqlObj g(std::move(_s));
                auto sch = g.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
                if (ntype == 3) {
                    // 跨服文本消息
                    std::string content = payload.isMember("content")
                                          ? payload["content"].asString() : "";
                    sch.getTable("offline_messages")
                       .insert("from_uid", "to_uid", "content", "type")
                       .values(from_uid, to_uid, content, 0)
                       .execute();
                } else if (ntype == 2) {
                    // 好友接受通知
                    Json::Value p;
                    p["from_uid"]  = from_uid;
                    p["from_name"] = from_name;
                    sch.getTable("offline_messages")
                       .insert("from_uid", "to_uid", "content", "type")
                       .values(from_uid, to_uid, p.toStyledString(), 2)
                       .execute();
                } else {
                    // 好友申请通知
                    Json::Value p;
                    p["from_uid"]  = from_uid;
                    p["from_name"] = from_name;
                    p["message"]   = msg_text;
                    sch.getTable("offline_messages")
                       .insert("from_uid", "to_uid", "content", "type")
                       .values(from_uid, to_uid, p.toStyledString(), 1)
                       .execute();
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[DeliverAddFriend] offline save error: " << e.what() << std::endl;
        }
        reply->set_error(ErrorCodes::Success);
        reply->set_msg("Saved offline");
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
