//
// Created by EDPerson on 2026/2/9.
//

#ifndef WXCHAT_MYSQLPOOL_H
#define WXCHAT_MYSQLPOOL_H
#include <mysqlx/xdevapi.h>
#include <vector>
#include "const.h"

class MysqlConPool {
public:
    static MysqlConPool* GetInstance();

    // 初始化池
    // host: "127.0.0.1", port: 33060 (注意不是3306)
    void Init(int poolSize);

    // 获取一个会话 (连接)
    std::unique_ptr<mysqlx::Session> GetSession();

    // 归还会话
    void ReturnSession(std::unique_ptr<mysqlx::Session> sess);
    std::string GetSchema() const { return schema_; }
    void Close();

private:
    MysqlConPool();
    ~MysqlConPool();

    // 内部帮助函数：创建一个新 Session
    std::unique_ptr<mysqlx::Session> CreateSession();

    // 队列里存放 Session 的智能指针
    std::queue<std::unique_ptr<mysqlx::Session>> pool_;

    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> b_stop_;

    // 配置信息
    std::string host_;
    int port_;
    std::string user_;
    std::string pwd_;
    std::string schema_;
};

// 业务数据访问层 (DAO)
class MysqlDao {
public:
    MysqlDao();
    ~MysqlDao();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo);
    bool CheckUserPwd(const std::string& user, const std::string& pwd, UserInfo& userInfo);
    std::string GetEmailByName(const std::string& name);
    bool ResetPwd(const std::string& name, const std::string& new_pwd);
    bool GetUserByUid(int uid, UserInfo& userInfo);

    // ── 好友关系 ──────────────────────────────────────────────────────────
    // 向 chat_friends 表建立双向好友关系（INSERT IGNORE，幂等）
    bool AddFriendRelation(int uid1, int uid2);

    // 获取 uid 的全部好友列表
    bool GetFriendsByUid(int uid, std::vector<UserInfo>& friends);
};

// RAII 自动归还器 (类似于你的 std::lock_guard)
class MysqlObj {
public:
    // 接管 session 所有权
    MysqlObj(std::unique_ptr<mysqlx::Session> sess) : _sess(std::move(sess)) {}

    // 析构时自动归还
    ~MysqlObj() {
        if (_sess) {
            MysqlConPool::GetInstance()->ReturnSession(std::move(_sess));
        }
    }

    // 暴露指针供调用
    mysqlx::Session* get() { return _sess.get(); }

private:
    std::unique_ptr<mysqlx::Session> _sess;
};


#endif //WXCHAT_MYSQLPOOL_H