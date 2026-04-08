#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DTO/JsonUtils.h"
#include "const.h"

namespace dto {

struct GroupMemberBrief {
    int uid = 0;
    std::string name;
    std::string avatar_path;
    int role = 2;
    int status = 1;
};

struct CreateGroupReq {
    std::string group_name;
    std::vector<int> member_uids;
};

struct CreateGroupRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    std::string group_name;
    int member_count = 0;
    std::string msg;
};

struct GroupInfoReq {
    uint64_t group_id = 0;
};

struct GroupInfoRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    int owner_uid = 0;
    std::string group_name;
    std::string avatar_path;
    std::string notice;
    int member_count = 0;
    std::string msg;
};

struct GroupMemberListReq {
    uint64_t group_id = 0;
};

struct GroupMemberListRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    std::vector<GroupMemberBrief> members;
    std::string msg;
};

struct InviteGroupMemberReq {
    uint64_t group_id = 0;
    std::vector<int> member_uids;
};

struct InviteGroupMemberRsp {
    int error = ErrorCodes::Success;
    int added_member_count = 0;
    std::string msg;
};

struct QuitGroupReq {
    uint64_t group_id = 0;
};

struct QuitGroupRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    std::string msg;
};

// ── Dissolve group (owner only) ──
struct DissolveGroupReq {
    uint64_t group_id = 0;
};

struct DissolveGroupRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    std::string msg;
};

// ── Transfer ownership (owner only) ──
struct TransferOwnerReq {
    uint64_t group_id = 0;
    int new_owner_uid = 0;
};

struct TransferOwnerRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    std::string msg;
};

// ── Kick member (owner/admin only) ──
struct KickMemberReq {
    uint64_t group_id = 0;
    int target_uid = 0;
};

struct KickMemberRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    std::string msg;
};

// ── Set mute (owner/admin only) ──
struct SetMuteReq {
    uint64_t group_id = 0;
    int target_uid = 0;
    int64_t mute_until_ts = 0;  // 0 = unmute
};

struct SetMuteRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    std::string msg;
};

struct GroupTextMsgReq {
    uint64_t group_id = 0;
    std::string content;
};

struct GroupTextMsgRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    uint64_t msg_id = 0;
    int64_t timestamp = 0;
    std::string msg;
};

struct GroupSyncReq {
    uint64_t group_id = 0;
    uint64_t last_msg_id = 0;
};

struct GroupMessageItem {
    uint64_t msg_id = 0;
    uint64_t group_id = 0;
    int sender_uid = 0;
    int msg_type = 0;
    std::string content;
    int64_t timestamp = 0;
};

struct GroupSyncRsp {
    int error = ErrorCodes::Success;
    uint64_t group_id = 0;
    uint64_t latest_msg_id = 0;
    std::vector<GroupMessageItem> messages;
    std::string msg;
};

inline bool ParseUidArray(const Json::Value& root,
                          const char* key,
                          std::vector<int>& out) {
    if (!root.isObject() || !root.isMember(key) || !root[key].isArray()) {
        return false;
    }

    out.clear();
    for (const auto& value : root[key]) {
        int uid = 0;
        if (value.isInt()) {
            uid = value.asInt();
        } else if (value.isString()) {
            try {
                uid = std::stoi(value.asString());
            } catch (...) {
                return false;
            }
        } else {
            return false;
        }
        out.push_back(uid);
    }
    return true;
}

inline Json::Value ToJson(const GroupMemberBrief& member) {
    Json::Value root;
    root["uid"] = member.uid;
    root["name"] = member.name;
    root["avatar_path"] = member.avatar_path;
    root["role"] = member.role;
    root["status"] = member.status;
    return root;
}

inline Json::Value ToJson(const GroupMessageItem& item) {
    Json::Value root;
    root["msg_id"] = std::to_string(item.msg_id);
    root["group_id"] = std::to_string(item.group_id);
    root["sender_uid"] = item.sender_uid;
    root["msg_type"] = item.msg_type;
    root["content"] = item.content;
    root["timestamp"] = static_cast<Json::Int64>(item.timestamp);
    return root;
}

inline bool FromJson(const Json::Value& root, CreateGroupReq& out) {
    return json::GetRequiredString(root, "group_name", out.group_name) &&
           ParseUidArray(root, "member_uids", out.member_uids);
}

inline bool FromJson(const Json::Value& root, GroupInfoReq& out) {
    return json::GetRequiredUInt64(root, "group_id", out.group_id);
}

inline bool FromJson(const Json::Value& root, GroupMemberListReq& out) {
    return json::GetRequiredUInt64(root, "group_id", out.group_id);
}

inline bool FromJson(const Json::Value& root, InviteGroupMemberReq& out) {
    return json::GetRequiredUInt64(root, "group_id", out.group_id) &&
           ParseUidArray(root, "member_uids", out.member_uids);
}

inline bool FromJson(const Json::Value& root, QuitGroupReq& out) {
    return json::GetRequiredUInt64(root, "group_id", out.group_id);
}

inline bool FromJson(const Json::Value& root, DissolveGroupReq& out) {
    return json::GetRequiredUInt64(root, "group_id", out.group_id);
}

inline bool FromJson(const Json::Value& root, TransferOwnerReq& out) {
    return json::GetRequiredUInt64(root, "group_id", out.group_id) &&
           json::GetRequiredInt(root, "new_owner_uid", out.new_owner_uid);
}

inline bool FromJson(const Json::Value& root, KickMemberReq& out) {
    return json::GetRequiredUInt64(root, "group_id", out.group_id) &&
           json::GetRequiredInt(root, "target_uid", out.target_uid);
}

inline bool FromJson(const Json::Value& root, SetMuteReq& out) {
    if (!json::GetRequiredUInt64(root, "group_id", out.group_id) ||
        !json::GetRequiredInt(root, "target_uid", out.target_uid)) {
        return false;
    }
    out.mute_until_ts = json::GetOptionalInt64(root, "mute_until_ts", 0);
    return true;
}

inline bool FromJson(const Json::Value& root, GroupTextMsgReq& out) {
    if (!json::GetRequiredUInt64(root, "group_id", out.group_id) ||
        !json::GetRequiredString(root, "content", out.content)) {
        return false;
    }
    // Guard against oversized messages (4KB max)
    if (out.content.empty() || out.content.size() > 4096) {
        return false;
    }
    return true;
}

inline bool FromJson(const Json::Value& root, GroupSyncReq& out) {
    if (!json::GetRequiredUInt64(root, "group_id", out.group_id)) {
        return false;
    }
    out.last_msg_id = json::GetOptionalUInt64(root, "last_msg_id", 0);
    return true;
}

inline Json::Value ToJson(const CreateGroupRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    if (!rsp.group_name.empty()) {
        root["group_name"] = rsp.group_name;
    }
    if (rsp.member_count > 0) {
        root["member_count"] = rsp.member_count;
    }
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const GroupInfoRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    if (rsp.owner_uid > 0) {
        root["owner_uid"] = rsp.owner_uid;
    }
    root["group_name"] = rsp.group_name;
    root["avatar_path"] = rsp.avatar_path;
    root["notice"] = rsp.notice;
    root["member_count"] = rsp.member_count;
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const GroupMemberListRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    Json::Value members(Json::arrayValue);
    for (const auto& member : rsp.members) {
        members.append(ToJson(member));
    }
    root["members"] = members;
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const InviteGroupMemberRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    root["added_member_count"] = rsp.added_member_count;
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const QuitGroupRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const DissolveGroupRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const TransferOwnerRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const KickMemberRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const SetMuteRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const GroupTextMsgRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    if (rsp.msg_id > 0) {
        root["msg_id"] = std::to_string(rsp.msg_id);
    }
    if (rsp.timestamp > 0) {
        root["timestamp"] = static_cast<Json::Int64>(rsp.timestamp);
    }
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

inline Json::Value ToJson(const GroupSyncRsp& rsp) {
    Json::Value root;
    root["error"] = rsp.error;
    if (rsp.group_id > 0) {
        root["group_id"] = std::to_string(rsp.group_id);
    }
    root["latest_msg_id"] = std::to_string(rsp.latest_msg_id);
    Json::Value messages(Json::arrayValue);
    for (const auto& item : rsp.messages) {
        messages.append(ToJson(item));
    }
    root["messages"] = messages;
    if (!rsp.msg.empty()) {
        root["msg"] = rsp.msg;
    }
    return root;
}

}  // namespace dto
