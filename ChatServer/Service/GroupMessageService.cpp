#include "Service/GroupMessageService.h"

#include <ctime>

dto::GroupTextMsgRsp GroupMessageService::SendTextMessage(
    int sender_uid,
    const dto::GroupTextMsgReq& req,
    dto::GroupMessageItem* out_message,
    std::vector<int>* out_member_uids) const {
    dto::GroupTextMsgRsp rsp;
    if (sender_uid <= 0 || req.group_id == 0 || req.content.empty()) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid group text message";
        return rsp;
    }

    GroupInfoRecord info;
    if (!_repo.GetGroupInfo(req.group_id, info) || info.status != 1) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }
    if (!_repo.IsGroupMember(req.group_id, sender_uid)) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "You are not a member of this group";
        return rsp;
    }

    // Check mute status
    int64_t mute_until_ts = 0;
    if (_repo.GetMemberMuteUntil(req.group_id, sender_uid, mute_until_ts) &&
        mute_until_ts > static_cast<int64_t>(std::time(nullptr))) {
        rsp.error = ErrorCodes::GroupMuted;
        rsp.msg = "You are muted in this group";
        return rsp;
    }

    uint64_t msg_id = 0;
    int64_t timestamp = 0;
    std::vector<int> member_uids;
    if (!_repo.SaveTextMessage(req.group_id, sender_uid, req.content, msg_id, timestamp, member_uids)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Failed to save group message";
        return rsp;
    }

    rsp.group_id = req.group_id;
    rsp.msg_id = msg_id;
    rsp.timestamp = timestamp;
    rsp.msg = "ok";

    if (out_message) {
        out_message->msg_id = msg_id;
        out_message->group_id = req.group_id;
        out_message->sender_uid = sender_uid;
        out_message->msg_type = 0;
        out_message->content = req.content;
        out_message->timestamp = timestamp;
    }
    if (out_member_uids) {
        *out_member_uids = std::move(member_uids);
    }
    return rsp;
}

dto::GroupSyncRsp GroupMessageService::PullSync(int owner_uid,
                                                const dto::GroupSyncReq& req) const {
    dto::GroupSyncRsp rsp;
    rsp.group_id = req.group_id;
    if (owner_uid <= 0 || req.group_id == 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid group sync request";
        return rsp;
    }

    GroupInfoRecord info;
    if (!_repo.GetGroupInfo(req.group_id, info) || info.status != 1) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }
    if (!_repo.IsGroupMember(req.group_id, owner_uid)) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "You are not a member of this group";
        return rsp;
    }

    std::vector<GroupMessageRecord> records;
    if (!_repo.PullMessages(owner_uid, req.group_id, req.last_msg_id, records, rsp.latest_msg_id)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Failed to pull group sync messages";
        return rsp;
    }

    rsp.messages.reserve(records.size());
    for (const auto& record : records) {
        dto::GroupMessageItem item;
        item.msg_id = record.msg_id;
        item.group_id = record.group_id;
        item.sender_uid = record.sender_uid;
        item.msg_type = record.msg_type;
        item.content = record.content;
        item.timestamp = record.timestamp;
        rsp.messages.push_back(std::move(item));
    }
    rsp.msg = "ok";
    return rsp;
}
