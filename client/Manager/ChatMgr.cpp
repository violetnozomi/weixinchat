#include "ChatMgr.h"
#include "ChatStorage.h"
#include <algorithm>

ChatMgr::ChatMgr() = default;

// ── 初始化 ──────────────────────────────────────────────────────────────────────

void ChatMgr::Init(int selfUid)
{
    ChatStorage::GetInstance()->Init(selfUid);
    _db_loaded.clear();
}
// ── SQLite 惰性加载 ─────────────────────────────────────────────────────────────────────

void ChatMgr::EnsureLoaded(int peerUid)
{
    if (_db_loaded.contains(peerUid)) return;
    _db_loaded.insert(peerUid);

    auto rows = ChatStorage::GetInstance()->LoadHistory(peerUid);
    if (!rows.isEmpty()) {
        _histories[peerUid] = rows;
    }
}
// ── 追加消息 ─────────────────────────────────────────────────────────────────

void ChatMgr::AppendMessage(const MessageInfo &msg)
{
    // 1. 确定 peerUid
    const int peerUid = msg.isSelf ? msg.toUid : msg.fromUid;

    // 2. 持久化到 SQLite
    ChatStorage::GetInstance()->SaveMessage(msg);

    // 3. 内存中更新

    _histories[peerUid].append(msg);

    // 仅当该会话不是当前活动会话时才增加未读数
    if (peerUid != _active_peer_uid && !msg.isSelf) {
        _unread[peerUid]++;
        emit sig_unread_changed(peerUid, _unread[peerUid]);
        emit sig_total_unread_changed(TotalUnread());
    }

    emit sig_message_appended(peerUid, msg);
}

// ── 历史记录获取 ──────────────────────────────────────────────────────────────

const QVector<MessageInfo> &ChatMgr::GetHistory(int peerUid)
{
    EnsureLoaded(peerUid);
    auto it = _histories.find(peerUid);
    if (it == _histories.end()) return _empty_vec;
    return it.value();
}

MessageInfo ChatMgr::GetLastMessage(int peerUid) const
{
    auto it = _histories.find(peerUid);
    if (it == _histories.end() || it.value().isEmpty()) return MessageInfo{};
    return it.value().last();
}

QList<int> ChatMgr::GetConversationList() const
{
    QList<int> keys = _histories.keys();
    // 按最后一条消息的时间倒序排列（最近的排在最前面）
    std::sort(keys.begin(), keys.end(), [this](int a, int b) {
        return GetLastMessage(a).time > GetLastMessage(b).time;
    });
    return keys;
}

// ── 未读计数 ─────────────────────────────────────────────────────────────────

int ChatMgr::TotalUnread() const
{
    int total = 0;
    for (auto count : _unread)
        total += count;
    return total;
}

void ChatMgr::MarkAsRead(int peerUid)
{
    if (_unread.value(peerUid, 0) == 0) return;
    _unread[peerUid] = 0;
    emit sig_unread_changed(peerUid, 0);
    emit sig_total_unread_changed(TotalUnread());
}

// ── 清理 ─────────────────────────────────────────────────────────────────────

void ChatMgr::Clear()
{
    _histories.clear();
    _unread.clear();
    _active_peer_uid = -1;
    _db_loaded.clear();
    ChatStorage::GetInstance()->Close();
}
