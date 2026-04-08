#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct GroupInfoRecord {
    uint64_t group_id = 0;
    int owner_uid = 0;
    std::string group_name;
    std::string avatar_path;
    std::string notice;
    int status = 1;
    int member_count = 0;
};

struct GroupMemberRecord {
    int uid = 0;
    std::string name;
    std::string avatar_path;
    int role = 2;
    int status = 1;
};

struct GroupMessageRecord {
    uint64_t msg_id = 0;
    uint64_t group_id = 0;
    int sender_uid = 0;
    int msg_type = 0;
    std::string content;
    int64_t timestamp = 0;
};

class GroupRepository {
public:
    bool CreateGroup(int owner_uid,
                     const std::string& group_name,
                     const std::vector<int>& member_uids,
                     uint64_t& out_group_id) const;

    bool GetGroupInfo(uint64_t group_id, GroupInfoRecord& out) const;
    bool GetGroupMembers(uint64_t group_id, std::vector<GroupMemberRecord>& out) const;
    bool IsGroupMember(uint64_t group_id, int uid) const;
    bool GetMemberRole(uint64_t group_id, int uid, int& out_role) const;
    bool AddMembers(uint64_t group_id,
                    const std::vector<int>& member_uids,
                    std::vector<int>& added_uids) const;
    bool MarkMemberQuit(uint64_t group_id, int uid) const;
    bool GetMemberMuteUntil(uint64_t group_id, int uid, int64_t& out_mute_until_ts) const;
    bool DissolveGroup(uint64_t group_id, int owner_uid) const;
    bool TransferOwnership(uint64_t group_id, int old_owner_uid, int new_owner_uid) const;
    bool KickMember(uint64_t group_id, int target_uid) const;
    bool SetMemberMute(uint64_t group_id, int target_uid, int64_t mute_until_ts) const;
    bool SaveTextMessage(uint64_t group_id,
                         int sender_uid,
                         const std::string& content,
                         uint64_t& out_msg_id,
                         int64_t& out_timestamp,
                         std::vector<int>& out_active_member_uids) const;
    bool PullMessages(int owner_uid,
                      uint64_t group_id,
                      uint64_t since_msg_id,
                      std::vector<GroupMessageRecord>& out,
                      uint64_t& latest_msg_id) const;
};
