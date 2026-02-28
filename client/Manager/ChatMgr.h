#ifndef CHATMGR_H
#define CHATMGR_H

#include "Singleton.h"
#include "MessageInfo.h"
#include <QObject>
#include <QMap>
#include <QSet>
#include <QList>
#include <QVector>

// ── 聊天消息管理器（单例）────────────────────────────────────────────────────
//
// 职责：
//   · 维护所有会话的消息历史  QMap<peerUid, QVector<MessageInfo>>
//   · 维护每个会话的未读计数  QMap<peerUid, int>
//   · 记录"当前打开的会话"，接收新消息时自动决定是否增加未读
//   · 发射信号供 chatDialog 和 Chat_User_List 响应
//
// UI 层（chatdialog、Chat_User_List）只负责渲染，
// 不再自己维护 QVector/QMap，从本类读取即可。

class ChatMgr : public QObject,
                public Singleton<ChatMgr>,
                public std::enable_shared_from_this<ChatMgr>
{
    Q_OBJECT
    friend class Singleton<ChatMgr>;

public:
    ~ChatMgr() = default;

    // ── 初始化 ────────────────────────────────────────────────────────────
    // 登录成功后调用（打开 SQLite，同时设置 selfUid）
    void Init(int selfUid);

    // ── 会话操作 ─────────────────────────────────────────────────────────

    // 追加一条消息（来自 TcpMgr 收到的或自己发送的均调用此接口）
    // 同时持久化到本地 SQLite
    void AppendMessage(const MessageInfo &msg);

    // 确保某会话的历史已从 SQLite 加载到内存（SwitchToChat 前调用）
    void EnsureLoaded(int peerUid);

    // 获取与 peerUid 的完整历史（只读，内部自动 EnsureLoaded）
    const QVector<MessageInfo> &GetHistory(int peerUid);

    // 获取最后一条消息（用于会话列表预览）
    MessageInfo GetLastMessage(int peerUid) const;

    // 是否存在该会话
    bool HasConversation(int peerUid) const { return _histories.contains(peerUid); }

    // 所有已有会话的 peerUid 列表（按最近消息倒序）
    QList<int> GetConversationList() const;

    // ── 未读计数 ─────────────────────────────────────────────────────────

    // 某会话的未读数
    int  GetUnreadCount(int peerUid) const { return _unread.value(peerUid, 0); }

    // 总未读数
    int  TotalUnread() const;

    // 将某会话标记为已读（通常在用户点击打开时调用）
    void MarkAsRead(int peerUid);

    // ── 活动会话 ─────────────────────────────────────────────────────────

    // 设置当前用户正在查看的会话（新消息到达时不增加该 uid 的未读数）
    void SetActivePeer(int peerUid) { _active_peer_uid = peerUid; }
    int  GetActivePeer()      const { return _active_peer_uid;    }

    // ── 清理 ──────────────────────────────────────────────────────────────

    // 登出时清空所有数据并关闭 SQLite
    void Clear();

signals:
    // 有新消息追加（无论来自自己还是对方）
    void sig_message_appended(int peerUid, MessageInfo msg);

    // 某会话未读数变化（peerUid, newCount）
    void sig_unread_changed(int peerUid, int newCount);

    // 总未读数变化（供侧边栏角标使用）
    void sig_total_unread_changed(int total);

private:
    ChatMgr();

    // conversations：key = peerUid（对方 uid；自己发出的消息也按对方 uid 归档）
    QMap<int, QVector<MessageInfo>> _histories;

    // 未读计数
    QMap<int, int>  _unread;

    // 当前正在查看的会话（-1 = 没有打开任何聊天）
    int _active_peer_uid = -1;

    // 已从 SQLite 加载过历史的 peerUid 集合
    QSet<int> _db_loaded;

    // 为 GetHistory 提供空容器引用（避免返回悬空引用）
    QVector<MessageInfo> _empty_vec;
};

#endif // CHATMGR_H
