//
// Created by EDPerson on 2026/2/9.
//

#include "MySqlPool.h"
#include "HashUtils.h"
using namespace mysqlx;

// ================= 连接池实现 =================

MysqlConPool* MysqlConPool::GetInstance() {
    static MysqlConPool pool;
    return &pool;
}

MysqlConPool::MysqlConPool() : b_stop_(false) {}

MysqlConPool::~MysqlConPool() {
    Close();
}

void MysqlConPool::Init(int poolSize) {
    auto & cfg = ConfigMgr::Inst();
    host_ = cfg["Mysql"]["Host"];
    port_ = std::stoi(cfg["Mysql"]["Port"]);
    user_ = cfg["Mysql"]["User"];
    pwd_ = cfg["Mysql"]["Passwd"];
    schema_ = cfg["Mysql"]["Schema"];

    int success_count = 0;
    for (int i = 0; i < poolSize; ++i) {
        auto sess = CreateSession();
        if (sess) {
            pool_.push(std::move(sess));
            success_count++;
        }
    }
    
    std::cout << "MySQL连接池初始化: " << success_count << "/" << poolSize << " 连接就绪" << std::endl;
    
    if (success_count == 0) {
        std::cerr << "错误: MySQL连接失败，请检查配置 (" << host_ << ":" << port_ << ")" << std::endl;
    }
}

std::unique_ptr<Session> MysqlConPool::CreateSession() {
    try {
        SessionSettings settings(host_, port_, user_, pwd_);
        settings.set(SessionOption::DB, schema_);
        return std::make_unique<Session>(settings);
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "MySQL连接失败: " << err.what() << std::endl;
        return nullptr;
    }
}

std::unique_ptr<Session> MysqlConPool::GetSession() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] {
        if (b_stop_) return true;
        return !pool_.empty();
    });

    if (b_stop_) return nullptr;

    auto sess = std::move(pool_.front());
    pool_.pop();
    return sess;
}

void MysqlConPool::ReturnSession(std::unique_ptr<Session> sess) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (b_stop_) return;

    // 检查 session 是否还活着，死了就重连一个
    // 注意：这里简单判断，实际生产可以用 sess->sql("SELECT 1").execute() 测试
    if (!sess) {
        sess = CreateSession();
    }

    pool_.push(std::move(sess));
    cond_.notify_one();
}

void MysqlConPool::Close() {
    b_stop_ = true;
    cond_.notify_all();
    std::unique_lock<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        pool_.pop(); // unique_ptr 自动析构关闭连接
    }
}

// ================= DAO 业务实现 =================

MysqlDao::MysqlDao() {}
MysqlDao::~MysqlDao() {}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (sess == nullptr) {
        std::cerr << "获取MySQL连接失败" << std::endl;
        return -1;
    }

    MysqlObj poolObj(std::move(sess));

    try {
        Schema db = poolObj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
        Table userTable = db.getTable("chat_user");

        // 检查用户名是否存在
        RowResult nameCheck = userTable.select("uid")
                                      .where("name = :name")
                                      .bind("name", name)
                                      .execute();
        if (nameCheck.fetchOne()) {
            return 0;  // 用户名已存在
        }

        // 检查邮箱是否存在
        RowResult emailCheck = userTable.select("uid")
                                       .where("email = :email")
                                       .bind("email", email)
                                       .execute();
        if (emailCheck.fetchOne()) {
            return -2;  // 邮箱已存在
        }

        // 事务：生成新ID并插入
        poolObj.get()->sql("START TRANSACTION").execute();
        
        try {
            poolObj.get()->sql("UPDATE user_id SET id = id + 1").execute();
            SqlResult idResult = poolObj.get()->sql("SELECT id FROM user_id").execute();
            Row idRow = idResult.fetchOne();
            if (!idRow) {
                poolObj.get()->sql("ROLLBACK").execute();
                std::cerr << "生成用户ID失败" << std::endl;
                return -1;
            }
            
            int newUid = idRow[0];

            // 生成盐值并对密码进行 SHA-256 哈希，存储格式: "salt$sha256hash"
            std::string salt       = GenerateSalt();
            std::string hashedPwd  = HashPassword(pwd, salt);
            std::string storedPwd  = salt + "$" + hashedPwd;

            userTable.insert("uid", "name", "email", "pwd")
                    .values(newUid, name, email, storedPwd)
                    .execute();
            poolObj.get()->sql("COMMIT").execute();
            
            std::cout << "用户注册成功: uid=" << newUid << ", name=" << name << std::endl;
            return newUid;
            
        } catch (const mysqlx::Error& err) {
            poolObj.get()->sql("ROLLBACK").execute();
            std::cerr << "注册事务失败: " << err.what() << std::endl;
            return -1;
        }
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "注册异常: " << err.what() << std::endl;
        return -1;
    }
}
std::string MysqlDao::GetEmailByName(const std::string& name) {
    // 1. 从连接池获取 Session
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (sess == nullptr) {
        std::cerr << "GetEmailByName: 获取MySQL连接失败" << std::endl;
        return "";
    }

    // 2. 使用 RAII 包装器管理 Session (确保函数结束时连接自动归还给池子)
    MysqlObj poolObj(std::move(sess));

    try {
        // 3. 获取 Schema 和 Table
        // 统一使用 "chat_user" 表（与数据库初始化脚本一致）
        Schema db = poolObj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
        Table userTable = db.getTable("chat_user");

        // 4. 构建查询 (X DevAPI 风格)
        // SQL 等价于: SELECT email FROM user WHERE name = 'name'
        RowResult result = userTable.select("email")
                                    .where("name = :name") // 使用命名参数绑定
                                    .bind("name", name)
                                    .execute();

        // 5. 获取结果
        Row row = result.fetchOne(); // 获取第一行
        if (!row) {
            return ""; // 没找到该用户
        }

        // 6. 提取数据
        // row[0] 对应 select 中的第一个字段 "email"
        std::string email = (std::string)row[0];
        return email;
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "GetEmailByName 异常: " << err.what() << std::endl;
        return "";
    }
}
bool MysqlDao::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (sess == nullptr) return false;

    MysqlObj poolObj(std::move(sess));

    try {
        Schema db = poolObj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema()); // 获取库
        Table userTable = db.getTable("chat_user"); // 获取表（统一使用chat_user）

        // 3. 查询数据，包含密码字段
        // SELECT name, uid, email, pwd FROM chat_user WHERE email = ?
        RowResult result = userTable.select("name", "uid", "email", "pwd")
                                    .where("email = :email") // 这里的 :email 是命名参数
                                    .bind("email", email)    // 绑定参数
                                    .execute();

        // 4. 获取结果
        Row row = result.fetchOne(); // 只取一行
        if (!row) {
            return false; // 没查到用户
        }

        // 5. 验证密码（支持 "salt$hash" 新格式和旧版明文的兼容验证）
        std::string db_pwd = (std::string)row[3];
        if (!VerifyPassword(pwd, db_pwd)) {
            return false; // 密码错误
        }

        // 6. 密码正确，填充用户信息
        userInfo.name = (std::string)row[0];
        userInfo.uid = (int)row[1];
        userInfo.email = (std::string)row[2];
        userInfo.pwd = db_pwd;

        return true;
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "CheckPwd Error: " << err << std::endl;
        return false;
    }
}

/**
 * 验证用户登录（支持用户名或邮箱）
 * @param user 用户名或邮箱
 * @param pwd 密码
 * @param userInfo 输出参数，验证成功后填充用户信息
 * @return true-验证成功 false-验证失败
 */
bool MysqlDao::CheckUserPwd(const std::string& user, const std::string& pwd, UserInfo& userInfo) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (sess == nullptr) return false;

    MysqlObj poolObj(std::move(sess));

    try {
        Schema db = poolObj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
        Table userTable = db.getTable("chat_user");

        // 判断user是邮箱还是用户名（简单判断：包含@符号则认为是邮箱）
        std::string whereClause;
        if (user.find('@') != std::string::npos) {
            whereClause = "email = :user";
        } else {
            whereClause = "name = :user";
        }

        // 查询用户信息（包括密码）
        RowResult result = userTable.select("name", "uid", "email", "pwd")
                                    .where(whereClause)
                                    .bind("user", user)
                                    .execute();

        Row row = result.fetchOne();
        if (!row) {
            std::cout << "用户不存在: " << user << std::endl;
            return false; // 用户不存在
        }

        // 验证密码（支持 "salt$hash" 新格式和旧版明文的兼容验证）
        std::string db_pwd = (std::string)row[3];
        if (!VerifyPassword(pwd, db_pwd)) {
            std::cout << "密码错误，用户: " << user << std::endl;
            return false; // 密码错误
        }

        // 验证成功，填充用户信息
        userInfo.name = (std::string)row[0];
        userInfo.uid = (int)row[1];
        userInfo.email = (std::string)row[2];
        userInfo.pwd = db_pwd;

        std::cout << "登录验证成功: uid=" << userInfo.uid << ", name=" << userInfo.name << std::endl;
        return true;
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "CheckUserPwd Error: " << err << std::endl;
        return false;
    }
}

/**
 * 重置用户密码
 * @param name 用户名
 * @param new_pwd 新密码
 * @return true-更新成功 false-更新失败
 * 
 * MySQL基础知识：
 * 1. UPDATE语句用于修改表中的数据
 * 2. WHERE子句指定要更新哪些行，非常重要！不加WHERE会更新所有行！
 * 3. 命名参数绑定(:name)可以防止SQL注入攻击
 */
bool MysqlDao::ResetPwd(const std::string& name, const std::string& new_pwd) {
    // 1. 从连接池获取数据库会话（连接）
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (sess == nullptr) {
        std::cerr << "ResetPwd: 获取MySQL连接失败" << std::endl;
        return false;
    }

    // 2. 使用RAII自动管理连接（函数结束时自动归还连接池）
    MysqlObj poolObj(std::move(sess));

    try {
        // 3. 获取数据库Schema和表对象
        Schema db = poolObj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
        Table userTable = db.getTable("chat_user");

        // 4. 对新密码进行哈希处理（生成新盐值），然后执行 UPDATE
        std::string salt      = GenerateSalt();
        std::string hashed    = HashPassword(new_pwd, salt);
        std::string storedPwd = salt + "$" + hashed;

        Result updateResult = userTable.update()
                                      .set("pwd", storedPwd)    // 存储哈希后的密码
                                      .where("name = :name")    // 指定更新条件
                                      .bind("name", name)       // 绑定参数
                                      .execute();

        // 5. 检查更新是否成功
        // getAffectedItemsCount() 返回受影响的行数
        uint64_t affected = updateResult.getAffectedItemsCount();
        if (affected > 0) {
            std::cout << "密码重置成功: 用户=" << name << ", 影响行数=" << affected << std::endl;
            return true;
        } else {
            std::cerr << "密码重置失败: 用户不存在 name=" << name << std::endl;
            return false;
        }
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "ResetPwd异常: " << err.what() << std::endl;
        return false;
    }
}

/**
 * 根据uid获取用户信息
 * @param uid 用户ID
 * @param userInfo 输出参数，查询成功后填充用户信息
 * @return 成功返回true，失败返回false
 */
bool MysqlDao::GetUserByUid(int uid, UserInfo& userInfo) {
    // 1. 从连接池获取 Session
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (sess == nullptr) {
        std::cerr << "GetUserByUid: 获取MySQL连接失败" << std::endl;
        return false;
    }

    // 2. 使用 RAII 包装器管理 Session
    MysqlObj poolObj(std::move(sess));

    try {
        // 3. 获取 Schema 和 Table
        Schema db = poolObj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema());
        Table userTable = db.getTable("chat_user");

        // 4. 查询用户信息
        // SELECT name, uid, email FROM chat_user WHERE uid = :uid
        RowResult result = userTable.select("name", "uid", "email")
                                    .where("uid = :uid")
                                    .bind("uid", uid)
                                    .execute();

        // 5. 获取结果
        Row row = result.fetchOne();
        if (!row) {
            std::cerr << "GetUserByUid: 用户不存在 uid=" << uid << std::endl;
            return false;
        }

        // 6. 填充用户信息
        userInfo.name = (std::string)row[0];
        userInfo.uid = (int)row[1];
        userInfo.email = (std::string)row[2];
        
        std::cout << "GetUserByUid成功: uid=" << uid << ", name=" << userInfo.name << std::endl;
        return true;
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "GetUserByUid异常: " << err.what() << std::endl;
        return false;
    }
}

// ── 建立双向好友关系 ──────────────────────────────────────────────────────────
bool MysqlDao::AddFriendRelation(int uid1, int uid2) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) {
        std::cerr << "[DAO] AddFriendRelation: 获取MySQL连接失败" << std::endl;
        return false;
    }
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
        auto* s = guard.get();
        // INSERT IGNORE 保证幂等（duplicate key 不报错）
        s->sql("INSERT IGNORE INTO `" + schema + "`.`chat_friends`"
               " (user_uid, friend_uid) VALUES (?, ?)")
           .bind(uid1, uid2).execute();
        s->sql("INSERT IGNORE INTO `" + schema + "`.`chat_friends`"
               " (user_uid, friend_uid) VALUES (?, ?)")
           .bind(uid2, uid1).execute();
        std::cout << "[DAO] AddFriendRelation: " << uid1 << " <-> " << uid2 << std::endl;
        return true;
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "[DAO] AddFriendRelation error: " << err.what() << std::endl;
        return false;
    }
}

// ── 获取好友列表 ──────────────────────────────────────────────────────────────
bool MysqlDao::GetFriendsByUid(int uid, std::vector<UserInfo>& friends) {
    auto sess = MysqlConPool::GetInstance()->GetSession();
    if (!sess) return false;
    MysqlObj guard(std::move(sess));
    try {
        const std::string schema = MysqlConPool::GetInstance()->GetSchema();
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
    }
    catch (const mysqlx::Error& err) {
        std::cerr << "[DAO] GetFriendsByUid error: " << err.what() << std::endl;
        return false;
    }
}
