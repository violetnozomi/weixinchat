#pragma once

#include "DTO/GroupDTO.h"
#include "Repository/GroupRepository.h"
#include "Singleton.h"

class GroupService : public Singleton<GroupService> {
    friend class Singleton<GroupService>;

public:
    dto::CreateGroupRsp CreateGroup(int owner_uid, const dto::CreateGroupReq& req) const;
    dto::GroupInfoRsp GetGroupInfo(int requester_uid, const dto::GroupInfoReq& req) const;

private:
    GroupService() = default;

    GroupRepository _repo;
};
