//
// ChatServer MySQL DAO 实现
//
#include "MySqlPool.h"
#include "ConfigMgr.h"
#include <iostream>

using namespace mysqlx;

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
    cond_.wait(lock, [this]{ return b_stop_ || !pool_.empty(); });
    if (b_stop_) return nullptr;
    auto s = std::move(pool_.front());
    pool_.pop();
    return s;
}

void MysqlConPool::ReturnSession(std::unique_ptr<Session> s) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (b_stop_) return;
    if (!s) s = CreateSession();
    pool_.push(std::move(s));
    cond_.notify_one();
}

void MysqlConPool::Close() {
    b_stop_ = true;
    cond_.notify_all();
    std::unique_lock<std::mutex> lk(mutex_);
    while (!pool_.empty()) pool_.pop();
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
        RowResult r = tbl.select("id")
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

bool MysqlDao::SaveFileTransfer(int sender_uid, int receiver_uid,
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
            "(sender_uid, receiver_uid, file_key, filename, file_size, download_url, is_image) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)")
            .bind(sender_uid)
            .bind(receiver_uid)
            .bind(file_key)
            .bind(filename)
            .bind(file_size)
            .bind(download_url)
            .bind(is_image ? 1 : 0)
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
            "       file_size, download_url, is_image "
            "FROM `" + schema + "`.`file_transfers` "
            "WHERE receiver_uid = ? AND delivered = 0 "
            "ORDER BY created_at ASC")
            .bind(uid).execute();
        for (Row row : r.fetchAll()) {
            FileTransferRecord rec;
            rec.id           = (int)row[0];
            rec.sender_uid   = (int)row[1];
            rec.receiver_uid = (int)row[2];
            rec.file_key     = (std::string)row[3];
            rec.filename     = (std::string)row[4];
            rec.file_size    = (int64_t)row[5];
            rec.download_url = (std::string)row[6];
            rec.is_image     = ((int)row[7] != 0);
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

