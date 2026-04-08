//
// ChatServer MySQL DAO 实现
//
#include "MySqlPool.h"
#include "ConfigMgr.h"
#include "SnowflakeID.h"
#include <iostream>
#include <chrono>

using namespace mysqlx;

namespace {

bool TableExists(Session& sess, const std::string& schema, const std::string& table) {
    SqlResult r = sess.sql(
        "SELECT COUNT(*) "
        "FROM information_schema.TABLES "
        "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?")
        .bind(schema)
        .bind(table)
        .execute();
    Row row = r.fetchOne();
    return row && static_cast<int64_t>(row[0]) > 0;
}

bool ColumnExists(Session& sess,
                  const std::string& schema,
                  const std::string& table,
                  const std::string& column) {
    SqlResult r = sess.sql(
        "SELECT COUNT(*) "
        "FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND COLUMN_NAME = ?")
        .bind(schema)
        .bind(table)
        .bind(column)
        .execute();
    Row row = r.fetchOne();
    return row && static_cast<int64_t>(row[0]) > 0;
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// MysqlConPool
// ──────────────────────────────────────────────────────────────────────────────

MysqlConPool* MysqlConPool::GetInstance() {
    static MysqlConPool pool;
    return &pool;
}

MysqlConPool::MysqlConPool() : b_stop_(false) {}
MysqlConPool::~MysqlConPool() { Close(); }

void MysqlConPool::Init(int poolSize) {
    auto& cfg = ConfigMgr::Inst();
    host_   = cfg["MySQL"]["Host"];
    port_   = std::stoi(cfg["MySQL"]["Port"]);
    user_   = cfg["MySQL"]["User"];
    pwd_    = cfg["MySQL"]["Password"];
    schema_ = cfg["MySQL"]["Database"];

    if (auto migration = CreateSession()) {
        try {
            EnsureSchemaCompat(*migration);
        } catch (const std::exception& e) {
            std::cerr << "[MySQL-ChatServer] schema compat check failed: "
                      << e.what() << std::endl;
        }
    }

    int ok = 0;
    for (int i = 0; i < poolSize; ++i) {
        auto s = CreateSession();
        if (s) { pool_.push(std::move(s)); ++ok; }
    }
    std::cout << "[MySQL-ChatServer] 连接池: " << ok << "/" << poolSize << " 就绪" << std::endl;
}

std::unique_ptr<Session> MysqlConPool::CreateSession() {
    try {
        SessionSettings ss(host_, port_, user_, pwd_);
        ss.set(SessionOption::DB, schema_);
        return std::make_unique<Session>(ss);
    } catch (const mysqlx::Error& e) {
        std::cerr << "[MySQL-ChatServer] 连接失败: " << e.what() << std::endl;
        return nullptr;
    }
}

std::unique_ptr<Session> MysqlConPool::GetSession() {
    std::unique_lock<std::mutex> lock(mutex_);
    // 最多等待 3 秒，超时返回 nullptr；防止所有连接被长查询占满时调用方永久挂死
    bool ok = cond_.wait_for(lock, std::chrono::seconds(3),
                             [this]{ return b_stop_ || !pool_.empty(); });
    if (!ok || b_stop_) {
        std::cerr << "[MySQL-ChatServer] GetSession: "
                  << (b_stop_ ? "pool shutdown" : "timeout (3s)") << std::endl;
        return nullptr;
    }
    auto s = std::move(pool_.front());
    pool_.pop();
    return s;
}

void MysqlConPool::ReturnSession(std::unique_ptr<Session> s) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (b_stop_) {
        // 停机时仍正常销毁 session（unique_ptr 析构即可）
        return;
    }
    if (!s) {
        s = CreateSession();
        if (!s) return; // 重连失败不放回池
    }
    pool_.push(std::move(s));
    cond_.notify_one();
}

void MysqlConPool::Close() {
    b_stop_ = true;
    cond_.notify_all();
    std::unique_lock<std::mutex> lk(mutex_);
    while (!pool_.empty()) pool_.pop();
}

void MysqlConPool::EnsureSchemaCompat(Session& sess) {
    const std::string offlineTable = "`" + schema_ + "`.`offline_messages`";
    const std::string contentTable = "`" + schema_ + "`.`message_content`";
    const std::string timelineTable = "`" + schema_ + "`.`user_timeline`";

    if (TableExists(sess, schema_, "offline_messages") &&
        !ColumnExists(sess, schema_, "offline_messages", "delivered_at")) {
        sess.sql(
            "ALTER TABLE " + offlineTable +
            " ADD COLUMN `delivered_at` DATETIME NULL DEFAULT NULL AFTER `delivered`")
            .execute();
        std::cout << "[MySQL-ChatServer] upgraded offline_messages.delivered_at" << std::endl;
    }

    if (!TableExists(sess, schema_, "message_content")) {
        sess.sql(
            "CREATE TABLE IF NOT EXISTS " + contentTable + " ("
            " `msg_id` BIGINT NOT NULL,"
            " `sender_uid` INT NOT NULL,"
            " `msg_type` TINYINT NOT NULL DEFAULT 0,"
            " `content` TEXT NOT NULL,"
            " `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " PRIMARY KEY (`msg_id`),"
            " INDEX `idx_mc_sender` (`sender_uid`, `msg_id`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci")
            .execute();
        std::cout << "[MySQL-ChatServer] created message_content" << std::endl;
    }

    if (!TableExists(sess, schema_, "user_timeline")) {
        sess.sql(
            "CREATE TABLE IF NOT EXISTS " + timelineTable + " ("
            " `owner_uid` INT NOT NULL,"
            " `msg_id` BIGINT NOT NULL,"
            " `peer_uid` INT NOT NULL,"
            " `direction` TINYINT NOT NULL DEFAULT 1,"
            " `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " PRIMARY KEY (`owner_uid`, `msg_id`),"
            " INDEX `idx_ut_conv` (`owner_uid`, `peer_uid`, `msg_id`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci")
            .execute();
        std::cout << "[MySQL-ChatServer] created user_timeline" << std::endl;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// MysqlDao
// ──────────────────────────────────────────────────────────────────────────────

bool MysqlDao::GetUserByUid(int uid, UserInfo& out) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        Schema db  = guard.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
        Table  tbl = db.getTable("chat_user");
        RowResult r = tbl.select("uid","name","email")
                        .where("uid = :uid")
                        .bind("uid", uid)
                        .execute();
        Row row = r.fetchOne();
        if (!row) return false;
        out.uid   = (int)row[0];
        out.name  = (std::string)row[1];
        out.email = (std::string)row[2];
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] GetUserByUid error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::AddFriendRelation(int uid1, int uid2) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        // chat_friends (user_uid, friend_uid)，UNIQUE KEY (user_uid, friend_uid)
        // 用 INSERT IGNORE 保证幂等
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        // 双向好友 — 两条记录
        auto s = guard.get();
        s->sql("INSERT IGNORE INTO `" + schema + "`.`chat_friends`"
               " (user_uid, friend_uid) VALUES (?, ?)")
           .bind(uid1, uid2).execute();
        s->sql("INSERT IGNORE INTO `" + schema + "`.`chat_friends`"
               " (user_uid, friend_uid) VALUES (?, ?)")
           .bind(uid2, uid1).execute();
        std::cout << "[DAO] AddFriendRelation: " << uid1 << " <-> " << uid2 << std::endl;
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] AddFriendRelation error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::DeleteFriendRelation(int uid1, int uid2) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        // 双向删除
        guard.get()->sql(
            "DELETE FROM `" + schema + "`.`chat_friends` "
            "WHERE (user_uid = ? AND friend_uid = ?) "
            "   OR (user_uid = ? AND friend_uid = ?)")
            .bind(uid1).bind(uid2).bind(uid2).bind(uid1).execute();
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] DeleteFriendRelation error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::BlockFriend(int blocker_uid, int blocked_uid) {
    // 1. 删除好友关系
    if (!DeleteFriendRelation(blocker_uid, blocked_uid)) return false;
    // 2. 插入屏蔽记录（表 blocked_users 若不存在则自动忽略错误）
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return true;   // 好友关系已删，屏蔽表插入失败不致命
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        guard.get()->sql(
            "INSERT IGNORE INTO `" + schema + "`.`blocked_users` "
            "(blocker_uid, blocked_uid) VALUES (?, ?)")
            .bind(blocker_uid).bind(blocked_uid).execute();
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] BlockFriend insert error: " << e.what() << std::endl;
        return true;   // 非致命
    }
}

bool MysqlDao::AreFriends(int uid1, int uid2) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        Schema db  = guard.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
        Table  tbl = db.getTable("chat_friends");
        // 注意：新 schema 中 chat_friends 的主键是 (user_uid, friend_uid)，
        // 原来的 id 列已移除，改查 user_uid 字段存在性
        RowResult r = tbl.select("user_uid")
                        .where("user_uid = :u AND friend_uid = :f")
                        .bind("u", uid1).bind("f", uid2)
                        .execute();
        return r.fetchOne().operator bool();
    } catch (...) { return false; }
}

bool MysqlDao::GetFriendsByUid(int uid, std::vector<UserInfo>& friends) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        // JOIN chat_friends with chat_user to get friend info in one query
        SqlResult r = guard.get()->sql(
            "SELECT u.uid, u.name, u.email "
            "FROM `" + schema + "`.`chat_friends` f "
            "JOIN `" + schema + "`.`chat_user` u ON f.friend_uid = u.uid "
            "WHERE f.user_uid = ?").bind(uid).execute();
        for (Row row : r.fetchAll()) {
            UserInfo info;
            info.uid   = (int)row[0];
            info.name  = (std::string)row[1];
            info.email = (std::string)row[2];
            friends.push_back(info);
        }
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] GetFriendsByUid error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::UpdateUserProfile(int uid, const std::string& name,
                                  const std::string& signature,
                                  const std::string& avatarPath) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        guard.get()->sql(
            "UPDATE `" + schema + "`.`chat_user` "
            "SET name = ?, signature = ?, avatar_path = ? "
            "WHERE uid = ?")
            .bind(name).bind(signature).bind(avatarPath).bind(uid).execute();
        std::cout << "[DAO] UpdateUserProfile uid=" << uid
                  << " name=" << name << std::endl;
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] UpdateUserProfile error: " << e.what() << std::endl;
        return false;
    }
}

// ── MinIO 文件传输 DAO ────────────────────────────────────────────────────

bool MysqlDao::SaveFileTransfer(int sender_uid, int receiver_uid, bool is_voice, int duration_ms,
                                 const std::string& file_key,
                                 const std::string& filename,
                                 int64_t file_size,
                                 const std::string& download_url,
                                 bool is_image)
{
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        guard.get()->sql(
            "INSERT INTO `" + schema + "`.`file_transfers` "
            "(id, sender_uid, receiver_uid, file_key, filename, file_size, download_url, is_image, is_voice, duration_ms) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
            .bind(NewSnowflakeID())
            .bind(sender_uid)
            .bind(receiver_uid)
            .bind(file_key)
            .bind(filename)
            .bind(file_size)
            .bind(download_url)
            .bind(is_image ? 1 : 0)
            .bind(is_voice ? 1 : 0)
            .bind(duration_ms)
            .execute();
        std::cout << "[DAO] SaveFileTransfer: " << file_key
                  << " sender=" << sender_uid << " receiver=" << receiver_uid << std::endl;
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] SaveFileTransfer error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::GetOfflineFiles(int uid, std::vector<FileTransferRecord>& out) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        SqlResult r = guard.get()->sql(
            "SELECT id, sender_uid, receiver_uid, file_key, filename, "
            "       file_size, download_url, is_image, is_voice, duration_ms "
            "FROM `" + schema + "`.`file_transfers` "
            "WHERE receiver_uid = ? AND delivered = 0 "
            "ORDER BY created_at ASC")
            .bind(uid).execute();
        for (Row row : r.fetchAll()) {
            FileTransferRecord rec;
            rec.id           = static_cast<int64_t>(row[0]);  // BIGINT
            rec.sender_uid   = (int)row[1];
            rec.receiver_uid = (int)row[2];
            rec.file_key     = (std::string)row[3];
            rec.filename     = (std::string)row[4];
            rec.file_size    = (int64_t)row[5];
            rec.download_url = (std::string)row[6];
            rec.is_image     = ((int)row[7] != 0);
            rec.is_voice     = ((int)row[8] != 0);
            rec.duration_ms  = (int)row[9];
            out.push_back(rec);
        }
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] GetOfflineFiles error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::MarkFileDelivered(const std::string& file_key, int receiver_uid) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        guard.get()->sql(
            "UPDATE `" + schema + "`.`file_transfers` "
            "SET delivered = 1 "
            "WHERE file_key = ? AND receiver_uid = ?")
            .bind(file_key).bind(receiver_uid).execute();
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] MarkFileDelivered error: " << e.what() << std::endl;
        return false;
    }
}

// ── AI Agent 工具专用查询实现 ─────────────────────────────────────────────────

bool MysqlDao::SearchFriendsByName(int caller_uid,
                                    const std::string& name_keyword,
                                    int limit,
                                    std::vector<UserInfo>& out)
{
    if (caller_uid <= 0 || name_keyword.empty() || limit <= 0) return false;
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        // JOIN chat_friends 限定只查调用者的好友，非好友对结果不可见
        // CONCAT('%', ?, '%') 参数化通配符，防止 SQL 注入
        SqlResult r = guard.get()->sql(
            "SELECT u.uid, u.name, u.email "
            "FROM `" + schema + "`.`chat_friends` f "
            "JOIN `" + schema + "`.`chat_user` u ON f.friend_uid = u.uid "
            "WHERE f.user_uid = ? "
            "  AND u.name LIKE CONCAT('%', ?, '%') "
            "ORDER BY u.uid ASC "
            "LIMIT ?")
            .bind(caller_uid)
            .bind(name_keyword)
            .bind(limit)
            .execute();
        for (Row row : r.fetchAll()) {
            UserInfo info;
            info.uid   = static_cast<int>(row[0]);
            info.name  = row[1].get<std::string>();
            info.email = row[2].get<std::string>();
            out.push_back(info);
        }
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] SearchFriendsByName error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::SearchChatHistory(int uid1, int uid2,
                                  const std::string& keyword,
                                  int limit,
                                  std::vector<ChatRecord>& out)
{
    if (limit <= 0) return false;
    // 上限 20 条，防止 AI 上下文爆炸
    if (limit > 20) limit = 20;

    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();

        // 构造 SQL：双向匹配 + 关键词过滤 + 时间倒序 + 分页
        // 注意：X DevAPI bind() 不支持在 LIKE 中内嵌通配符，
        // 使用 CONCAT('%', ?, '%') 代替，既安全又兼容 MySQL 8+
        std::string sql =
            "SELECT from_uid, to_uid, content, "
            "       DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS ts "
            "FROM `" + schema + "`.`chat_messages` "
            "WHERE ((from_uid = ? AND to_uid = ?) OR (from_uid = ? AND to_uid = ?)) ";

        if (!keyword.empty()) {
            sql += "AND content LIKE CONCAT('%', ?, '%') ";
        }
        sql += "ORDER BY created_at DESC LIMIT ?";

        auto stmt = guard.get()->sql(sql)
                        .bind(uid1).bind(uid2)
                        .bind(uid2).bind(uid1);
        if (!keyword.empty()) stmt = stmt.bind(keyword);
        stmt = stmt.bind(limit);

        SqlResult r = stmt.execute();

        // 结果倒序返回，重新正序排列后写入 out
        std::vector<ChatRecord> tmp;
        for (Row row : r.fetchAll()) {
            ChatRecord rec;
            rec.from_uid   = static_cast<int>(row[0]);
            rec.to_uid     = static_cast<int>(row[1]);
            rec.content    = row[2].get<std::string>();
            rec.created_at = row[3].get<std::string>();
            tmp.push_back(std::move(rec));
        }
        // 数据库已按 DESC 排序，翻转回正序交给 AI
        std::reverse(tmp.begin(), tmp.end());
        out = std::move(tmp);
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] SearchChatHistory error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::GetFileTransfer(const std::string& file_key, FileTransferRecord& out) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        SqlResult r = guard.get()->sql(
            "SELECT id, sender_uid, receiver_uid, file_key, filename, "
            "       file_size, download_url, is_image "
            "FROM `" + schema + "`.`file_transfers` "
            "WHERE file_key = ? LIMIT 1")
            .bind(file_key).execute();
        Row row = r.fetchOne();
        if (!row) return false;
        out.id           = (int)row[0];
        out.sender_uid   = (int)row[1];
        out.receiver_uid = (int)row[2];
        out.file_key     = (std::string)row[3];
        out.filename     = (std::string)row[4];
        out.file_size    = (int64_t)row[5];
        out.download_url = (std::string)row[6];
        out.is_image     = ((int)row[7] != 0);
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] GetFileTransfer error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::FilterExistingUids(const std::vector<int>& candidate_uids,
                                   std::vector<int>& existing_uids) {
    if (candidate_uids.empty()) return true;

    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));

    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();

        // Build parameterized IN clause
        std::string placeholders;
        for (size_t i = 0; i < candidate_uids.size(); ++i) {
            if (i > 0) placeholders += ",";
            placeholders += "?";
        }

        std::string sql = "SELECT uid FROM `" + schema + "`.`chat_user` WHERE uid IN (" +
                          placeholders + ")";
        auto stmt = guard.get()->sql(sql);
        for (int uid : candidate_uids) {
            stmt.bind(uid);
        }
        auto result = stmt.execute();

        existing_uids.clear();
        for (auto row : result) {
            existing_uids.push_back(static_cast<int>(row[0]));
        }
        return true;
    } catch (const mysqlx::Error& e) {
        std::cerr << "[DAO] FilterExistingUids error: " << e.what() << std::endl;
        return false;
    }
}

