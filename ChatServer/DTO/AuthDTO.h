#pragma once

#include <string>
#include <vector>

#include "DTO/JsonUtils.h"
#include "const.h"

namespace dto {

struct ChatLoginReq {
    int uid = 0;
    std::string token;
    std::string user;
};

struct ChatLoginRsp {
    int error = ErrorCodes::Success;
    std::string msg;
};

struct FriendListItem {
    int uid = 0;
    std::string name;
    std::string email;
    std::string signature;
};

struct FriendsListPush {
    int error = ErrorCodes::Success;
    std::vector<FriendListItem> friends;
};

inline bool FromJson(const Json::Value& root, ChatLoginReq& out) {
    return json::GetRequiredInt(root, "uid", out.uid) &&
           json::GetRequiredString(root, "token", out.token) &&
           json::GetRequiredString(root, "user", out.user);
}

inline Json::Value ToJson(const ChatLoginRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const FriendsListPush& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    Json::Value friends(Json::arrayValue);
    for (const auto& item : rsp.friends) {
        Json::Value friend_json;
        friend_json["uid"] = item.uid;
        friend_json["name"] = item.name;
        friend_json["email"] = item.email;
        friend_json["signature"] = item.signature;
        friends.append(friend_json);
    }
    root["friends"] = friends;
    return root;
}

}  // namespace dto
