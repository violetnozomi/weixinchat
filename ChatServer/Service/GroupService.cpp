#include "Service/GroupService.h"

#include <set>
#include <vector>

namespace {

std::vector<int> NormalizeMemberList(int owner_uid, const std::vector<int>& member_uids) {
    std::set<int> deduped;
    for (int uid : member_uids) {
        if (uid > 0 && uid != owner_uid) {
            deduped.insert(uid);
        }
    }
    return std::vector<int>(deduped.begin(), deduped.end());
}

}  // namespace

dto::CreateGroupRsp GroupService::CreateGroup(int owner_uid,
                                              const dto::CreateGroupReq& req) const {
    dto::CreateGroupRsp rsp;
    if (owner_uid <= 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid owner uid";
        return rsp;
    }
    if (req.group_name.empty() || req.group_name.size() > 128) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid group_name";
        return rsp;
    }

    auto members = NormalizeMemberList(owner_uid, req.member_uids);
    if (members.empty()) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Group requires at least one other member";
        return rsp;
    }

    if (!_repo.CreateGroup(owner_uid, req.group_name, members, rsp.group_id)) {
        rsp.error = ErrorCodes::RPCFailed;
        rsp.msg = "Create group failed";
        return rsp;
    }

    rsp.group_name = req.group_name;
    rsp.member_count = static_cast<int>(members.size()) + 1;
    return rsp;
}

dto::GroupInfoRsp GroupService::GetGroupInfo(int requester_uid,
                                             const dto::GroupInfoReq& req) const {
    dto::GroupInfoRsp rsp;
    if (requester_uid <= 0 || req.group_id == 0) {
        rsp.error = ErrorCodes::InvalidRequest;
        rsp.msg = "Invalid group info request";
        return rsp;
    }

    GroupInfoRecord record;
    if (!_repo.GetGroupInfo(req.group_id, record)) {
        rsp.error = ErrorCodes::GroupNotFound;
        rsp.msg = "Group not found";
        return rsp;
    }

    if (!_repo.IsGroupMember(req.group_id, requester_uid)) {
        rsp.error = ErrorCodes::PermissionDenied;
        rsp.msg = "You are not a member of this group";
        return rsp;
    }

    rsp.group_id = record.group_id;
    rsp.owner_uid = record.owner_uid;
    rsp.group_name = record.group_name;
    rsp.avatar_path = record.avatar_path;
    rsp.notice = record.notice;
    rsp.member_count = record.member_count;
    return rsp;
}
