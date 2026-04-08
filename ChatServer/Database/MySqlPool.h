//
// ChatServer 使用的 MySQL DAO
// 仅包含好友功能所需的业务方法
//
#ifndef CHATSERVER_MYSQLPOOL_H
#define CHATSERVER_MYSQLPOOL_H

#include <mysqlx/xdevapi.h>
#include "const.h"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

// ── 用户信息 ──────────────────────────────────────────────────────────────
struct UserInfo {
    int         uid  = 0;
    std::string name;
    std::string email;
    std::string pwd;
};

// ── 连接池 ─────────────────────────────────────────────────────────────────
class MysqlConPool {
public:
    static MysqlConPool* GetInstance();

    void Init(int poolSize);
    std::unique_ptr<mysqlx::Session> GetSession();
    void ReturnSession(std::unique_ptr<mysqlx::Session> sess);
    std::string GetSchema() const { return schema_; }
    void Close();

private:
    MysqlConPool();
    ~MysqlConPool();
    std::unique_ptr<mysqlx::Session> CreateSession();
    void EnsureSchemaCompat(mysqlx::Session& sess);

    std::queue<std::unique_ptr<mysqlx::Session>> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> b_stop_;

    std::string host_;
    int   port_;
    std::string user_;
    std::string pwd_;
    std::string schema_;
};

// ── RAII Session 归还器 ────────────────────────────────────────────────────
class MysqlObj {
public:
    explicit MysqlObj(std::unique_ptr<mysqlx::Session> sess) : _sess(std::move(sess)) {}
    ~MysqlObj() {
        if (_sess) MysqlConPool::GetInstance()->ReturnSession(std::move(_sess));
    }
    mysqlx::Session* get() { return _sess.get(); }
private:
    std::unique_ptr<mysqlx::Session> _sess;
};

// ── 文件传输记录 ──────────────────────────────────────────────────────────
struct FileTransferRecord {
    int64_t     id           = 0;   // file_transfers.id 已为 BIGINT
    int         sender_uid   = 0;
    int         receiver_uid = 0;
    std::string file_key;
    std::string filename;
    int64_t     file_size    = 0;
    std::string download_url;
    bool        is_image     = false;
    bool        is_voice     = false;
    int         duration_ms  = 0;
};

// ── DAO ───────────────────────────────────────────────────────────────────
class MysqlDao {
public:
    MysqlDao()  = default;
    ~MysqlDao() = default;

    // 查询用户信息
    bool GetUserByUid(int uid, UserInfo& out);

    // 向 chat_friends 表写入双向好友关系（幂等：已存在则忽略）
    // 返回 true = 写入成功，false = 失败
    bool AddFriendRelation(int uid1, int uid2);

    // 删除好友关系（双向删除）
    bool DeleteFriendRelation(int uid1, int uid2);

    // 屏蔽好友：删除好友关系 + 向 blocked_users 表插入
    bool BlockFriend(int blocker_uid, int blocked_uid);

    // 判断两人是否已是好友
    bool AreFriends(int uid1, int uid2);

    // 获取 uid 的全部好友列表
    bool GetFriendsByUid(int uid, std::vector<UserInfo>& friends);

    // 修改用户资料（昵称、签名、头像路径）
    bool UpdateUserProfile(int uid, const std::string& name,
                           const std::string& signature,
                           const std::string& avatarPath);

    // ── MinIO 文件传输记录 ────────────────────────────────────────────────
    // 保存一条文件传输记录（上传完成后调用）
    bool SaveFileTransfer(int sender_uid, int receiver_uid, bool is_voice, int duration_ms,
                          const std::string& file_key,
                          const std::string& filename,
                          int64_t file_size,
                          const std::string& download_url,
                          bool is_image);

    // 取出 uid 未读的离线文件记录
    bool GetOfflineFiles(int uid, std::vector<FileTransferRecord>& out);

    // 标记文件记录为已投递
    bool MarkFileDelivered(const std::string& file_key, int receiver_uid);

    // 查询单条文件记录（用于生成新鲜的 download_url 时，仅需 key 即可）
    bool GetFileTransfer(const std::string& file_key, FileTransferRecord& out);

    // ── AI Agent 工具专用查询 ─────────────────────────────────────────────

    // 在 caller_uid 的好友列表中按昵称模糊搜索（最多返回 limit 条）
    // 结果严格限定在好友关系内，非好友不可见，天然解决越权和同名歧义
    bool SearchFriendsByName(int caller_uid,
                             const std::string& name_keyword,
                             int limit,
                             std::vector<UserInfo>& out);

    // 搜索两个用户之间的聊天记录（关键词过滤，最多 limit 条，按时间倒序）
    // 用于 search_chat_history 工具：AI 可帮用户检索历史消息
    struct ChatRecord {
        int         from_uid  = 0;
        int         to_uid    = 0;
        std::string content;
        std::string created_at;  // "YYYY-MM-DD HH:MM:SS"
    };
    bool SearchChatHistory(int uid1, int uid2,
                           const std::string& keyword,
                           int limit,
                           std::vector<ChatRecord>& out);

    // 批量校验 uid 列表中哪些实际存在于 chat_user 表
    bool FilterExistingUids(const std::vector<int>& candidate_uids,
                            std::vector<int>& existing_uids);
};

#endif // CHATSERVER_MYSQLPOOL_H
