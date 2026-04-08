#include "Service/GroupMemberService.h"

#include <set>

#include "MySqlPool.h"

dto::GroupMemberListRsp GroupMemberService::GetGroupMemberList(
    int requester_uid, const dto::GroupMemberListReq& req) const {
    dto::GroupMemberListRsp rsp;
    if (requester_uid <= 0 || req.group_id == 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid member list request";
        return rsp;
    }

    if (!_repo.IsGroupMember(req.group_id, requester_uid)) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "You are not a member of this group";
        return rsp;
    }

    std::vector<GroupMemberRecord> members;
    if (!_repo.GetGroupMembers(req.group_id, members)) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }

    rsp.group_id = req.group_id;
    for (const auto& member : members) {
        dto::GroupMemberBrief item;
        item.uid = member.uid;
        item.name = member.name;
        item.avatar_path = member.avatar_path;
        item.role = member.role;
        item.status = member.status;
        rsp.members.push_back(std::move(item));
    }
    return rsp;
}

dto::InviteGroupMemberRsp GroupMemberService::InviteMembers(
    int operator_uid, const dto::InviteGroupMemberReq& req) const {
    dto::InviteGroupMemberRsp rsp;
    if (operator_uid <= 0 || req.group_id == 0 || req.member_uids.empty()) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid invite request";
        return rsp;
    }
    if (!_repo.IsGroupMember(req.group_id, operator_uid)) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "You are not a member of this group";
        return rsp;
    }

    int role = 2;
    if (!_repo.GetMemberRole(req.group_id, operator_uid, role)) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }
    if (role > 1) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "Only owner or admin can invite members";
        return rsp;
    }

    std::set<int> deduped;
    for (int uid : req.member_uids) {
        if (uid > 0 && uid != operator_uid) {
            deduped.insert(uid);
        }
    }
    if (deduped.empty()) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "No valid members to invite";
        return rsp;
    }

    // Validate all users in a single batch query instead of N individual queries.
    // Users that don't exist are silently skipped — AddMembers will only insert valid rows.
    std::vector<int> normalized;
    {
        MysqlDao dao;
        std::vector<int> candidate(deduped.begin(), deduped.end());
        std::vector<int> valid_uids;
        if (!dao.FilterExistingUids(candidate, valid_uids)) {
            // Fallback: if batch check unavailable, accept all and let DB handle it
            normalized = std::move(candidate);
        } else {
            normalized = std::move(valid_uids);
        }
    }
    if (normalized.empty()) {
        rsp.error = ErrorCodes::UserNotExist;
        rsp.msg = "No valid users found to invite";
        return rsp;
    }

    std::vector<int> added_uids;
    if (!_repo.AddMembers(req.group_id, normalized, added_uids)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Failed to invite members";
        return rsp;
    }

    rsp.added_member_count = static_cast<int>(added_uids.size());
    rsp.msg = added_uids.empty() ? "No new members were added" : "Members added";
    return rsp;
}

dto::QuitGroupRsp GroupMemberService::QuitGroup(
    int operator_uid, const dto::QuitGroupReq& req) const {
    dto::QuitGroupRsp rsp;
    if (operator_uid <= 0 || req.group_id == 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid quit group request";
        return rsp;
    }
    if (!_repo.IsGroupMember(req.group_id, operator_uid)) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "You are not a member of this group";
        return rsp;
    }

    int role = 2;
    if (!_repo.GetMemberRole(req.group_id, operator_uid, role)) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }
    if (role == 0) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "Group owner cannot quit directly. Transfer ownership or dissolve the group first.";
        return rsp;
    }
    if (!_repo.MarkMemberQuit(req.group_id, operator_uid)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Failed to quit group";
        return rsp;
    }

    rsp.group_id = req.group_id;
    rsp.msg = "Quit group success";
    return rsp;
}

dto::DissolveGroupRsp GroupMemberService::DissolveGroup(
    int operator_uid, const dto::DissolveGroupReq& req) const {
    dto::DissolveGroupRsp rsp;
    if (operator_uid <= 0 || req.group_id == 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid dissolve group request";
        return rsp;
    }
    int role = 2;
    if (!_repo.GetMemberRole(req.group_id, operator_uid, role)) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }
    if (role != 0) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "Only group owner can dissolve the group";
        return rsp;
    }
    if (!_repo.DissolveGroup(req.group_id, operator_uid)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Failed to dissolve group";
        return rsp;
    }
    rsp.group_id = req.group_id;
    rsp.msg = "Group dissolved";
    return rsp;
}

dto::TransferOwnerRsp GroupMemberService::TransferOwnership(
    int operator_uid, const dto::TransferOwnerReq& req) const {
    dto::TransferOwnerRsp rsp;
    if (operator_uid <= 0 || req.group_id == 0 || req.new_owner_uid <= 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid transfer ownership request";
        return rsp;
    }
    if (operator_uid == req.new_owner_uid) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Cannot transfer ownership to yourself";
        return rsp;
    }
    int role = 2;
    if (!_repo.GetMemberRole(req.group_id, operator_uid, role)) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }
    if (role != 0) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "Only group owner can transfer ownership";
        return rsp;
    }
    if (!_repo.IsGroupMember(req.group_id, req.new_owner_uid)) {
        rsp.error = ErrorCodes::UserNotExist;
        rsp.msg = "Target user is not a group member";
        return rsp;
    }
    if (!_repo.TransferOwnership(req.group_id, operator_uid, req.new_owner_uid)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Failed to transfer ownership";
        return rsp;
    }
    rsp.group_id = req.group_id;
    rsp.msg = "Ownership transferred";
    return rsp;
}

dto::KickMemberRsp GroupMemberService::KickMember(
    int operator_uid, const dto::KickMemberReq& req) const {
    dto::KickMemberRsp rsp;
    if (operator_uid <= 0 || req.group_id == 0 || req.target_uid <= 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid kick member request";
        return rsp;
    }
    if (operator_uid == req.target_uid) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Cannot kick yourself, use quit group instead";
        return rsp;
    }
    int op_role = 2;
    if (!_repo.GetMemberRole(req.group_id, operator_uid, op_role)) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }
    if (op_role > 1) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "Only owner or admin can kick members";
        return rsp;
    }
    int target_role = 2;
    if (!_repo.GetMemberRole(req.group_id, req.target_uid, target_role)) {
        rsp.error = ErrorCodes::UserNotExist;
        rsp.msg = "Target user is not a group member";
        return rsp;
    }
    // Cannot kick someone with equal or higher privilege
    if (target_role <= op_role) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "Cannot kick member with equal or higher role";
        return rsp;
    }
    if (!_repo.KickMember(req.group_id, req.target_uid)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Failed to kick member";
        return rsp;
    }
    rsp.group_id = req.group_id;
    rsp.msg = "Member kicked";
    return rsp;
}

dto::SetMuteRsp GroupMemberService::SetMemberMute(
    int operator_uid, const dto::SetMuteReq& req) const {
    dto::SetMuteRsp rsp;
    if (operator_uid <= 0 || req.group_id == 0 || req.target_uid <= 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid mute request";
        return rsp;
    }
    int op_role = 2;
    if (!_repo.GetMemberRole(req.group_id, operator_uid, op_role)) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }
    if (op_role > 1) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "Only owner or admin can mute members";
        return rsp;
    }
    int target_role = 2;
    if (!_repo.GetMemberRole(req.group_id, req.target_uid, target_role)) {
        rsp.error = ErrorCodes::UserNotExist;
        rsp.msg = "Target user is not a group member";
        return rsp;
    }
    if (target_role <= op_role) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "Cannot mute member with equal or higher role";
        return rsp;
    }
    if (!_repo.SetMemberMute(req.group_id, req.target_uid, req.mute_until_ts)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Failed to set mute";
        return rsp;
    }
    rsp.group_id = req.group_id;
    rsp.msg = req.mute_until_ts > 0 ? "Member muted" : "Member unmuted";
    return rsp;
}
