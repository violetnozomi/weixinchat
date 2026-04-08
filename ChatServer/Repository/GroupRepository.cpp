#include "Repository/GroupRepository.h"

#include <set>
#include <string>
#include <ctime>

#include <mysqlx/xdevapi.h>

#include "MySqlPool.h"
#include "SnowflakeID.h"

using namespace mysqlx;

namespace {

std::string SchemaPrefix() {
    return "`" + MysqlConPool::GetInstance()->GetSchema() + "`.";
}

}  // namespace

bool GroupRepository::CreateGroup(int owner_uid,
                                  const std::string& group_name,
                                  const std::vector<int>& member_uids,
                                  uint64_t& out_group_id) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    auto* sql_sess = guard.get();
    const std::string schema = SchemaPrefix();
    std::set<int> members(member_uids.begin(), member_uids.end());
    members.insert(owner_uid);

    out_group_id = static_cast<uint64_t>(NewSnowflakeID());

    try {
        sql_sess->startTransaction();

        sql_sess->sql(
            "INSERT INTO " + schema + "`chat_group` "
            "(group_id, owner_uid, group_name, avatar_path, notice, status) "
            "VALUES (?, ?, ?, '', '', 1)")
            .bind(static_cast<int64_t>(out_group_id))
            .bind(owner_uid)
            .bind(group_name)
            .execute();

        for (int uid : members) {
            const int role = uid == owner_uid ? 0 : 2;
            sql_sess->sql(
                "INSERT INTO " + schema + "`chat_group_member` "
                "(group_id, uid, role, status) VALUES (?, ?, ?, 1)")
                .bind(static_cast<int64_t>(out_group_id))
                .bind(uid)
                .bind(role)
                .execute();

            sql_sess->sql(
                "INSERT INTO " + schema + "`group_read_state` "
                "(group_id, uid, last_read_msg_id) VALUES (?, ?, 0)")
                .bind(static_cast<int64_t>(out_group_id))
                .bind(uid)
                .execute();
        }

        sql_sess->commit();
        return true;
    } catch (const mysqlx::Error&) {
        try {
            sql_sess->rollback();
        } catch (...) {
        }
        return false;
    }
}

bool GroupRepository::GetGroupInfo(uint64_t group_id, GroupInfoRecord& out) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();

    try {
        SqlResult result = guard.get()->sql(
            "SELECT g.group_id, g.owner_uid, g.group_name, g.avatar_path, g.notice, g.status, "
            "COUNT(m.uid) AS member_count "
            "FROM " + schema + "`chat_group` g "
            "LEFT JOIN " + schema + "`chat_group_member` m "
            "ON g.group_id = m.group_id AND m.status = 1 "
            "WHERE g.group_id = ? "
            "GROUP BY g.group_id, g.owner_uid, g.group_name, g.avatar_path, g.notice, g.status")
            .bind(static_cast<int64_t>(group_id))
            .execute();

        Row row = result.fetchOne();
        if (!row) {
            return false;
        }

        out.group_id = static_cast<uint64_t>(static_cast<int64_t>(row[0]));
        out.owner_uid = static_cast<int>(row[1]);
        out.group_name = static_cast<std::string>(row[2]);
        out.avatar_path = row[3].isNull() ? "" : static_cast<std::string>(row[3]);
        out.notice = row[4].isNull() ? "" : static_cast<std::string>(row[4]);
        out.status = static_cast<int>(row[5]);
        out.member_count = static_cast<int>(row[6]);
        return true;
    } catch (const mysqlx::Error&) {
        return false;
    }
}

bool GroupRepository::GetGroupMembers(uint64_t group_id,
                                      std::vector<GroupMemberRecord>& out) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();

    try {
        SqlResult result = guard.get()->sql(
            "SELECT m.uid, u.name, u.avatar_path, m.role, m.status "
            "FROM " + schema + "`chat_group_member` m "
            "JOIN " + schema + "`chat_user` u ON u.uid = m.uid "
            "WHERE m.group_id = ? AND m.status = 1 "
            "ORDER BY m.role ASC, m.joined_at ASC")
            .bind(static_cast<int64_t>(group_id))
            .execute();

        out.clear();
        for (Row row : result.fetchAll()) {
            GroupMemberRecord item;
            item.uid = static_cast<int>(row[0]);
            item.name = static_cast<std::string>(row[1]);
            item.avatar_path = row[2].isNull() ? "" : static_cast<std::string>(row[2]);
            item.role = static_cast<int>(row[3]);
            item.status = static_cast<int>(row[4]);
            out.push_back(std::move(item));
        }
        return true;
    } catch (const mysqlx::Error&) {
        return false;
    }
}

bool GroupRepository::IsGroupMember(uint64_t group_id, int uid) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();

    try {
        SqlResult result = guard.get()->sql(
            "SELECT uid FROM " + schema + "`chat_group_member` "
            "WHERE group_id = ? AND uid = ? AND status = 1")
            .bind(static_cast<int64_t>(group_id))
            .bind(uid)
            .execute();
        return static_cast<bool>(result.fetchOne());
    } catch (const mysqlx::Error&) {
        return false;
    }
}

bool GroupRepository::GetMemberRole(uint64_t group_id, int uid, int& out_role) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();

    try {
        SqlResult result = guard.get()->sql(
            "SELECT role FROM " + schema + "`chat_group_member` "
            "WHERE group_id = ? AND uid = ? AND status = 1")
            .bind(static_cast<int64_t>(group_id))
            .bind(uid)
            .execute();
        Row row = result.fetchOne();
        if (!row) {
            return false;
        }
        out_role = static_cast<int>(row[0]);
        return true;
    } catch (const mysqlx::Error&) {
        return false;
    }
}

bool GroupRepository::AddMembers(uint64_t group_id,
                                 const std::vector<int>& member_uids,
                                 std::vector<int>& added_uids) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    auto* sql_sess = guard.get();
    const std::string schema = SchemaPrefix();

    try {
        sql_sess->startTransaction();
        added_uids.clear();

        for (int uid : member_uids) {
            Row row = sql_sess->sql(
                "SELECT status FROM " + schema + "`chat_group_member` "
                "WHERE group_id = ? AND uid = ?")
                .bind(static_cast<int64_t>(group_id))
                .bind(uid)
                .execute()
                .fetchOne();

            if (row && static_cast<int>(row[0]) == 1) {
                continue;
            }

            sql_sess->sql(
                "INSERT INTO " + schema + "`chat_group_member` "
                "(group_id, uid, role, status, mute_until, joined_at) "
                "VALUES (?, ?, 2, 1, NULL, CURRENT_TIMESTAMP) "
                "ON DUPLICATE KEY UPDATE role = VALUES(role), status = VALUES(status), "
                "mute_until = VALUES(mute_until), joined_at = VALUES(joined_at)")
                .bind(static_cast<int64_t>(group_id))
                .bind(uid)
                .execute();

            sql_sess->sql(
                "INSERT INTO " + schema + "`group_read_state` "
                "(group_id, uid, last_read_msg_id) VALUES (?, ?, 0) "
                "ON DUPLICATE KEY UPDATE last_read_msg_id = last_read_msg_id")
                .bind(static_cast<int64_t>(group_id))
                .bind(uid)
                .execute();

            added_uids.push_back(uid);
        }

        sql_sess->commit();
        return true;
    } catch (const mysqlx::Error&) {
        try {
            sql_sess->rollback();
        } catch (...) {
        }
        return false;
    }
}

bool GroupRepository::MarkMemberQuit(uint64_t group_id, int uid) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();

    try {
        auto result = guard.get()->sql(
            "UPDATE " + schema + "`chat_group_member` "
            "SET status = 2 WHERE group_id = ? AND uid = ? AND status = 1")
            .bind(static_cast<int64_t>(group_id))
            .bind(uid)
            .execute();
        return result.getAffectedItemsCount() > 0;
    } catch (const mysqlx::Error&) {
        return false;
    }
}

bool GroupRepository::GetMemberMuteUntil(uint64_t group_id, int uid,
                                         int64_t& out_mute_until_ts) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();
    try {
        SqlResult result = guard.get()->sql(
            "SELECT UNIX_TIMESTAMP(mute_until) FROM " + schema + "`chat_group_member` "
            "WHERE group_id = ? AND uid = ? AND status = 1 AND mute_until IS NOT NULL")
            .bind(static_cast<int64_t>(group_id))
            .bind(uid)
            .execute();
        Row row = result.fetchOne();
        if (!row || row[0].isNull()) {
            out_mute_until_ts = 0;
            return false;
        }
        out_mute_until_ts = static_cast<int64_t>(row[0]);
        return true;
    } catch (const mysqlx::Error&) {
        return false;
    }
}

bool GroupRepository::DissolveGroup(uint64_t group_id, int owner_uid) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;

    MysqlObj guard(std::move(sess));
    auto* sql_sess = guard.get();
    const std::string schema = SchemaPrefix();
    try {
        sql_sess->startTransaction();

        // Verify caller is still the owner
        SqlResult check = sql_sess->sql(
            "SELECT owner_uid FROM " + schema + "`chat_group` "
            "WHERE group_id = ? AND status = 1 FOR UPDATE")
            .bind(static_cast<int64_t>(group_id))
            .execute();
        Row row = check.fetchOne();
        if (!row || static_cast<int>(row[0]) != owner_uid) {
            sql_sess->rollback();
            return false;
        }

        // Mark group as dissolved (status=2)
        sql_sess->sql(
            "UPDATE " + schema + "`chat_group` SET status = 2 WHERE group_id = ?")
            .bind(static_cast<int64_t>(group_id))
            .execute();

        // Mark all members as quit (status=3 = kicked/dissolved)
        sql_sess->sql(
            "UPDATE " + schema + "`chat_group_member` SET status = 3 "
            "WHERE group_id = ? AND status = 1")
            .bind(static_cast<int64_t>(group_id))
            .execute();

        sql_sess->commit();
        return true;
    } catch (const mysqlx::Error&) {
        try { sql_sess->rollback(); } catch (...) {}
        return false;
    }
}

bool GroupRepository::TransferOwnership(uint64_t group_id,
                                        int old_owner_uid,
                                        int new_owner_uid) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;

    MysqlObj guard(std::move(sess));
    auto* sql_sess = guard.get();
    const std::string schema = SchemaPrefix();
    try {
        sql_sess->startTransaction();

        // Verify the new owner is an active member
        SqlResult check = sql_sess->sql(
            "SELECT uid FROM " + schema + "`chat_group_member` "
            "WHERE group_id = ? AND uid = ? AND status = 1 FOR UPDATE")
            .bind(static_cast<int64_t>(group_id))
            .bind(new_owner_uid)
            .execute();
        if (!check.fetchOne()) {
            sql_sess->rollback();
            return false;
        }

        // Update chat_group.owner_uid
        sql_sess->sql(
            "UPDATE " + schema + "`chat_group` SET owner_uid = ? "
            "WHERE group_id = ? AND owner_uid = ?")
            .bind(new_owner_uid)
            .bind(static_cast<int64_t>(group_id))
            .bind(old_owner_uid)
            .execute();

        // Demote old owner to regular member (role 2)
        sql_sess->sql(
            "UPDATE " + schema + "`chat_group_member` SET role = 2 "
            "WHERE group_id = ? AND uid = ?")
            .bind(static_cast<int64_t>(group_id))
            .bind(old_owner_uid)
            .execute();

        // Promote new owner (role 0)
        sql_sess->sql(
            "UPDATE " + schema + "`chat_group_member` SET role = 0 "
            "WHERE group_id = ? AND uid = ?")
            .bind(static_cast<int64_t>(group_id))
            .bind(new_owner_uid)
            .execute();

        sql_sess->commit();
        return true;
    } catch (const mysqlx::Error&) {
        try { sql_sess->rollback(); } catch (...) {}
        return false;
    }
}

bool GroupRepository::KickMember(uint64_t group_id, int target_uid) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();
    try {
        auto result = guard.get()->sql(
            "UPDATE " + schema + "`chat_group_member` "
            "SET status = 3 WHERE group_id = ? AND uid = ? AND status = 1")
            .bind(static_cast<int64_t>(group_id))
            .bind(target_uid)
            .execute();
        return result.getAffectedItemsCount() > 0;
    } catch (const mysqlx::Error&) {
        return false;
    }
}

bool GroupRepository::SetMemberMute(uint64_t group_id, int target_uid,
                                    int64_t mute_until_ts) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();
    try {
        if (mute_until_ts <= 0) {
            // Unmute
            guard.get()->sql(
                "UPDATE " + schema + "`chat_group_member` "
                "SET mute_until = NULL WHERE group_id = ? AND uid = ? AND status = 1")
                .bind(static_cast<int64_t>(group_id))
                .bind(target_uid)
                .execute();
        } else {
            guard.get()->sql(
                "UPDATE " + schema + "`chat_group_member` "
                "SET mute_until = FROM_UNIXTIME(?) WHERE group_id = ? AND uid = ? AND status = 1")
                .bind(mute_until_ts)
                .bind(static_cast<int64_t>(group_id))
                .bind(target_uid)
                .execute();
        }
        return true;
    } catch (const mysqlx::Error&) {
        return false;
    }
}

bool GroupRepository::SaveTextMessage(uint64_t group_id,
                                      int sender_uid,
                                      const std::string& content,
                                      uint64_t& out_msg_id,
                                      int64_t& out_timestamp,
                                      std::vector<int>& out_active_member_uids) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    auto* sql_sess = guard.get();
    const std::string schema = SchemaPrefix();

    try {
        out_msg_id = static_cast<uint64_t>(NewSnowflakeID());
        out_timestamp = static_cast<int64_t>(std::time(nullptr));

        sql_sess->startTransaction();

        // Query active members INSIDE the transaction so the member snapshot
        // is consistent with the timeline inserts that follow.
        SqlResult member_result = sql_sess->sql(
            "SELECT uid FROM " + schema + "`chat_group_member` "
            "WHERE group_id = ? AND status = 1 FOR UPDATE")
            .bind(static_cast<int64_t>(group_id))
            .execute();

        out_active_member_uids.clear();
        for (Row row : member_result.fetchAll()) {
            out_active_member_uids.push_back(static_cast<int>(row[0]));
        }
        if (out_active_member_uids.empty()) {
            sql_sess->rollback();
            return false;
        }

        sql_sess->sql(
            "INSERT INTO " + schema + "`group_message_content` "
            "(msg_id, group_id, sender_uid, msg_type, content, created_at) "
            "VALUES (?, ?, ?, 0, ?, FROM_UNIXTIME(?))")
            .bind(static_cast<int64_t>(out_msg_id))
            .bind(static_cast<int64_t>(group_id))
            .bind(sender_uid)
            .bind(content)
            .bind(out_timestamp)
            .execute();

        for (int uid : out_active_member_uids) {
            sql_sess->sql(
                "INSERT INTO " + schema + "`group_user_timeline` "
                "(owner_uid, group_id, msg_id, created_at) "
                "VALUES (?, ?, ?, FROM_UNIXTIME(?))")
                .bind(uid)
                .bind(static_cast<int64_t>(group_id))
                .bind(static_cast<int64_t>(out_msg_id))
                .bind(out_timestamp)
                .execute();
        }

        sql_sess->sql(
            "UPDATE " + schema + "`group_read_state` "
            "SET last_read_msg_id = ?, last_read_at = FROM_UNIXTIME(?) "
            "WHERE group_id = ? AND uid = ?")
            .bind(static_cast<int64_t>(out_msg_id))
            .bind(out_timestamp)
            .bind(static_cast<int64_t>(group_id))
            .bind(sender_uid)
            .execute();

        sql_sess->commit();
        return true;
    } catch (const mysqlx::Error&) {
        try {
            sql_sess->rollback();
        } catch (...) {
        }
        return false;
    }
}

bool GroupRepository::PullMessages(int owner_uid,
                                   uint64_t group_id,
                                   uint64_t since_msg_id,
                                   std::vector<GroupMessageRecord>& out,
                                   uint64_t& latest_msg_id) const {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        return false;
    }

    MysqlObj guard(std::move(sess));
    const std::string schema = SchemaPrefix();

    try {
        SqlResult result = guard.get()->sql(
            "SELECT c.msg_id, c.group_id, c.sender_uid, c.msg_type, c.content, "
            "UNIX_TIMESTAMP(c.created_at) "
            "FROM " + schema + "`group_user_timeline` t "
            "JOIN " + schema + "`group_message_content` c ON t.msg_id = c.msg_id "
            "WHERE t.owner_uid = ? AND t.group_id = ? AND t.msg_id > ? "
            "ORDER BY t.msg_id ASC LIMIT 200")
            .bind(owner_uid)
            .bind(static_cast<int64_t>(group_id))
            .bind(static_cast<int64_t>(since_msg_id))
            .execute();

        out.clear();
        latest_msg_id = since_msg_id;
        for (Row row : result.fetchAll()) {
            GroupMessageRecord item;
            item.msg_id = static_cast<uint64_t>(static_cast<int64_t>(row[0]));
            item.group_id = static_cast<uint64_t>(static_cast<int64_t>(row[1]));
            item.sender_uid = static_cast<int>(row[2]);
            item.msg_type = static_cast<int>(row[3]);
            item.content = static_cast<std::string>(row[4]);
            item.timestamp = static_cast<int64_t>(row[5]);
            latest_msg_id = item.msg_id;
            out.push_back(std::move(item));
        }
        return true;
    } catch (const mysqlx::Error&) {
        return false;
    }
}
