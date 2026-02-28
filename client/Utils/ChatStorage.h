#ifndef CHATSTORAGE_H
#define CHATSTORAGE_H

#include "Singleton.h"
#include "MessageInfo.h"
#include <QString>
#include <QVector>
#include <QSqlDatabase>

// ── 聊天消息本地持久化（SQLite）──────────────────────────────────────────────
// 设计原则：
//   · 每个登录用户在本地拥有独立数据库文件:
//       %AppData%/weixinchat/chat_<uid>.db
//   · 本地数据库不限期限存储（用户自己控制）
//   · 云端（offline_messages）只保留7天，到期由服务端登录时清理
//
// 使用方式：
//   ChatStorage::GetInstance()->Init(selfUid);    // 登录时调用
//   ChatStorage::GetInstance()->SaveMessage(msg); // 发送/接收时调用
//   ChatStorage::GetInstance()->LoadHistory(peerUid, limit); // 切换会话时调用
//   ChatStorage::GetInstance()->Close();          // 登出时调用
//
class ChatStorage : public Singleton<ChatStorage>
{
    friend class Singleton<ChatStorage>;

public:
    ~ChatStorage();

    // ── 初始化 / 关闭 ────────────────────────────────────────────────────
    // 登录成功后调用，selfUid 决定数据库文件名
    bool Init(int selfUid);

    // 登出时关闭连接（下次 Init 可重新打开）
    void Close();

    bool IsOpen() const { return _is_open; }

    // ── 写入 ────────────────────────────────────────────────────────────
    // 保存一条消息（msg.time 为空时自动填当前时刻）
    bool SaveMessage(const MessageInfo &msg);

    // ── 读取 ────────────────────────────────────────────────────────────
    // 读取与 peerUid 的最近 limit 条消息（按时间正序返回）
    QVector<MessageInfo> LoadHistory(int peerUid, int limit = 500);

    // ── 清理 ────────────────────────────────────────────────────────────
    // 删除本地数据库中超过 keepDays 天的所有消息
    // （在 Init 时自动调用一次，默认保留 365 天）
    int  DeleteOldMessages(int keepDays = 365);

    // ── 会话列表 ─────────────────────────────────────────────────────────
    // 获取所有存在记录的会话 peerUid 列表（按最后消息时间倒序）
    QList<int> GetConversationUids();

private:
    ChatStorage();

    bool createSchema();
    bool ensureOpen() const;

    QSqlDatabase _db;
    QString      _connection_name;
    bool         _is_open = false;
    int          _self_uid = -1;
};

#endif // CHATSTORAGE_H
