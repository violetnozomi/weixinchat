#include "ChatMgr.h"

#include "ChatStorage.h"

ChatMgr::ChatMgr() = default;

void ChatMgr::Init(int selfUid)
{
    ChatStorage::GetInstance()->Init(selfUid);
    _histories.clear();
    _unread.clear();
    _active_conversation_key.clear();
    _db_loaded.clear();
}

void ChatMgr::EnsureLoaded(const QString &conversationKey)
{
    if (_db_loaded.contains(conversationKey)) {
        return;
    }

    _db_loaded.insert(conversationKey);
    const auto rows = ChatStorage::GetInstance()->LoadHistory(conversationKey);
    if (!rows.isEmpty()) {
        _histories[conversationKey] = rows;
    }
}

bool ChatMgr::AppendMessage(const MessageInfo &msg)
{
    MessageInfo stored = msg;
    if (stored.conversationKey.isEmpty()) {
        if (stored.conversationType == ConversationType::Group || !stored.groupId.isEmpty()) {
            stored.conversationType = ConversationType::Group;
            stored.conversationKey = MakeGroupConversationKey(stored.groupId);
        } else {
            stored.conversationType = ConversationType::Peer;
            const int peerUid = stored.isSelf ? stored.toUid : stored.fromUid;
            stored.conversationKey = MakePeerConversationKey(peerUid);
        }
    } else if (IsGroupConversationKey(stored.conversationKey)) {
        stored.conversationType = ConversationType::Group;
        if (stored.groupId.isEmpty()) {
            stored.groupId = ExtractGroupIdFromConversationKey(stored.conversationKey);
        }
    } else {
        stored.conversationType = ConversationType::Peer;
    }

    if (!IsValidConversationKey(stored.conversationKey)) {
        qWarning() << "[ChatMgr] Ignore invalid conversation key:" << stored.conversationKey
                   << "from_uid=" << stored.fromUid << "to_uid=" << stored.toUid
                   << "is_self=" << stored.isSelf;
        return false;
    }

    if (!stored.msgId.isEmpty()) {
        EnsureLoaded(stored.conversationKey);
        const auto it = _histories.find(stored.conversationKey);
        if (it != _histories.end()) {
            for (const auto &existing : it.value()) {
                if (existing.msgId == stored.msgId) {
                    return false;
                }
            }
        }
    }

    ChatStorage::GetInstance()->SaveMessage(stored);
    _histories[stored.conversationKey].append(stored);

    if (stored.conversationKey != _active_conversation_key && !stored.isSelf) {
        _unread[stored.conversationKey]++;
        emit sig_unread_changed(stored.conversationKey, _unread[stored.conversationKey]);
        emit sig_total_unread_changed(TotalUnread());
    }

    emit sig_message_appended(stored.conversationKey, stored);
    return true;
}

bool ChatMgr::UpdatePendingGroupTextAck(const QString &conversationKey,
                                        const QString &serverMsgId,
                                        const QDateTime &serverTime)
{
    if (conversationKey.isEmpty() || serverMsgId.isEmpty()) {
        return false;
    }

    EnsureLoaded(conversationKey);
    if (!ChatStorage::GetInstance()->UpdatePendingGroupTextAck(
            conversationKey, serverMsgId, serverTime)) {
        return false;
    }

    auto it = _histories.find(conversationKey);
    if (it == _histories.end()) {
        return true;
    }

    for (MessageInfo &msg : it.value()) {
        if (msg.conversationType != ConversationType::Group ||
            !msg.isSelf ||
            msg.type != MessageInfo::Text ||
            !msg.msgId.isEmpty()) {
            continue;
        }

        msg.msgId = serverMsgId;
        if (serverTime.isValid()) {
            msg.time = serverTime;
        }
        return true;
    }

    return true;
}

const QVector<MessageInfo> &ChatMgr::GetHistory(const QString &conversationKey)
{
    EnsureLoaded(conversationKey);
    const auto it = _histories.find(conversationKey);
    if (it == _histories.end()) {
        return _empty_vec;
    }
    return it.value();
}

MessageInfo ChatMgr::GetLastMessage(const QString &conversationKey)
{
    EnsureLoaded(conversationKey);
    const auto it = _histories.find(conversationKey);
    if (it == _histories.end() || it.value().isEmpty()) {
        return MessageInfo{};
    }
    return it.value().last();
}

QList<QString> ChatMgr::GetConversationList()
{
    QList<QString> keys = ChatStorage::GetInstance()->GetConversationKeys();
    for (auto it = _histories.begin(); it != _histories.end(); ++it) {
        if (!IsValidConversationKey(it.key())) {
            continue;
        }
        if (!keys.contains(it.key())) {
            keys.append(it.key());
        }
    }
    return keys;
}

QString ChatMgr::GetLatestServerMsgId(const QString &conversationKey)
{
    EnsureLoaded(conversationKey);
    const auto it = _histories.find(conversationKey);
    if (it != _histories.end()) {
        const auto &history = it.value();
        for (int i = history.size() - 1; i >= 0; --i) {
            if (!history[i].msgId.isEmpty()) {
                return history[i].msgId;
            }
        }
    }
    return ChatStorage::GetInstance()->GetLatestServerMsgId(conversationKey);
}

int ChatMgr::TotalUnread() const
{
    int total = 0;
    for (auto count : _unread) {
        total += count;
    }
    return total;
}

void ChatMgr::MarkAsRead(const QString &conversationKey)
{
    if (_unread.value(conversationKey, 0) == 0) {
        return;
    }

    _unread[conversationKey] = 0;
    emit sig_unread_changed(conversationKey, 0);
    emit sig_total_unread_changed(TotalUnread());
}

void ChatMgr::Clear()
{
    _histories.clear();
    _unread.clear();
    _active_conversation_key.clear();
    _db_loaded.clear();
    ChatStorage::GetInstance()->Close();
}
