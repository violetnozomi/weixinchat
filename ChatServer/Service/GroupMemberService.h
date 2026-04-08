#pragma once

#include "DTO/GroupDTO.h"
#include "Repository/GroupRepository.h"
#include "Singleton.h"

class GroupMemberService : public Singleton<GroupMemberService> {
    friend class Singleton<GroupMemberService>;

public:
    dto::GroupMemberListRsp GetGroupMemberList(int requester_uid,
                                               const dto::GroupMemberListReq& req) const;
    dto::InviteGroupMemberRsp InviteMembers(int operator_uid,
                                            const dto::InviteGroupMemberReq& req) const;
    dto::QuitGroupRsp QuitGroup(int operator_uid, const dto::QuitGroupReq& req) const;
    dto::DissolveGroupRsp DissolveGroup(int operator_uid, const dto::DissolveGroupReq& req) const;
    dto::TransferOwnerRsp TransferOwnership(int operator_uid, const dto::TransferOwnerReq& req) const;
    dto::KickMemberRsp KickMember(int operator_uid, const dto::KickMemberReq& req) const;
    dto::SetMuteRsp SetMemberMute(int operator_uid, const dto::SetMuteReq& req) const;

private:
    GroupMemberService() = default;

    GroupRepository _repo;
};
