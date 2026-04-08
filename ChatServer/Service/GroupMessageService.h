#pragma once

#include "DTO/GroupDTO.h"
#include "Repository/GroupRepository.h"
#include "Singleton.h"

class GroupMessageService : public Singleton<GroupMessageService> {
    friend class Singleton<GroupMessageService>;

public:
    dto::GroupTextMsgRsp SendTextMessage(int sender_uid,
                                         const dto::GroupTextMsgReq& req,
                                         dto::GroupMessageItem* out_message = nullptr,
                                         std::vector<int>* out_member_uids = nullptr) const;
    dto::GroupSyncRsp PullSync(int owner_uid, const dto::GroupSyncReq& req) const;

private:
    GroupMessageService() = default;

    GroupRepository _repo;
};
