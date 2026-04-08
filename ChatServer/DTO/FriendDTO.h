#pragma once

#include <string>

#include "DTO/JsonUtils.h"
#include "const.h"

namespace dto {

struct AddFriendReq {
    int to_uid = 0;
    std::string message;
};

struct AddFriendRsp {
    int error = ErrorCodes::Success;
    std::string msg;
};

struct NotifyAddFriend {
    int from_uid = 0;
    std::string from_name;
    std::string message;
    int ntype = 0;
};

struct AcceptFriendReq {
    int peer_uid = 0;
};

struct AcceptFriendRsp {
    int error = ErrorCodes::Success;
    int friend_uid = 0;
    std::string friend_name;
    std::string msg;
};

struct NotifyAcceptFriend {
    int from_uid = 0;
    std::string from_name;
    int ntype = 0;
};

inline bool FromJson(const Json::Value& root, AddFriendReq& out) {
    return json::GetRequiredInt(root, "to_uid", out.to_uid) &&
           json::GetRequiredString(root, "message", out.message);
}

inline bool FromJson(const Json::Value& root, AcceptFriendReq& out) {
    return json::GetRequiredInt(root, "peer_uid", out.peer_uid);
}

inline Json::Value ToJson(const AddFriendRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const NotifyAddFriend& notif) {
    Json::Value root;
    root["from_uid"] = notif.from_uid;
    root["from_name"] = notif.from_name;
    root["message"] = notif.message;
    if (notif.ntype > 0) {
        root["__ntype"] = notif.ntype;
    }
    return root;
}

inline Json::Value ToJson(const AcceptFriendRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.friend_uid > 0) {
        root["friend_uid"] = rsp.friend_uid;
    }
    if (!rsp.friend_name.empty()) {
        root["friend_name"] = rsp.friend_name;
    }
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const NotifyAcceptFriend& notif) {
    Json::Value root;
    root["from_uid"] = notif.from_uid;
    root["from_name"] = notif.from_name;
    if (notif.ntype > 0) {
        root["__ntype"] = notif.ntype;
    }
    return root;
}

}  // namespace dto
