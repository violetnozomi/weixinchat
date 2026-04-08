#pragma once

#include <string>
#include <vector>

#include "DTO/JsonUtils.h"
#include "SyncService.h"
#include "const.h"

namespace dto {

struct SyncMsgReq {
    uint64_t last_sync_id = 0;
};

struct SyncMessageItem {
    int64_t msg_id = 0;
    int sender_uid = 0;
    int peer_uid = 0;
    int msg_type = 0;
    int direction = 0;
    std::string content;
    int64_t created_at_ms = 0;
};

struct SyncMsgRsp {
    int error = ErrorCodes::Success;
    std::string msg;
    uint64_t latest_sync_id = 0;
    std::vector<SyncMessageItem> messages;
};

inline bool FromJson(const Json::Value& root, SyncMsgReq& out) {
    out.last_sync_id = json::GetOptionalUInt64(root, "last_sync_id", 0);
    return root.isObject();
}

inline SyncMessageItem ToSyncMessageItem(const ::Message& msg) {
    SyncMessageItem item;
    item.msg_id = msg.msg_id;
    item.sender_uid = msg.sender_uid;
    item.peer_uid = msg.peer_uid;
    item.msg_type = msg.msg_type;
    item.direction = msg.direction;
    item.content = msg.content;
    item.created_at_ms = msg.created_at_ms;
    return item;
}

inline Json::Value ToJson(const SyncMsgRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    root["latest_sync_id"] = static_cast<Json::UInt64>(rsp.latest_sync_id);

    Json::Value messages(Json::arrayValue);
    for (const auto& item : rsp.messages) {
        Json::Value message_json;
        message_json["msg_id"] = static_cast<Json::Int64>(item.msg_id);
        message_json["sender_uid"] = item.sender_uid;
        message_json["peer_uid"] = item.peer_uid;
        message_json["msg_type"] = item.msg_type;
        message_json["direction"] = item.direction;
        message_json["content"] = item.content;
        message_json["created_at_ms"] = static_cast<Json::Int64>(item.created_at_ms);
        messages.append(message_json);
    }
    root["messages"] = messages;
    return root;
}

}  // namespace dto
