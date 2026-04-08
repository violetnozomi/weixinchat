#ifndef CHATMGR_H
#define CHATMGR_H

#include "MessageInfo.h"
#include "Singleton.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVector>

class ChatMgr : public QObject,
                public Singleton<ChatMgr>,
                public std::enable_shared_from_this<ChatMgr>
{
    Q_OBJECT
    friend class Singleton<ChatMgr>;

public:
    ~ChatMgr() = default;

    void Init(int selfUid);

    bool AppendMessage(const MessageInfo &msg);
    bool UpdatePendingGroupTextAck(const QString &conversationKey,
                                   const QString &serverMsgId,
                                   const QDateTime &serverTime);

    void EnsureLoaded(const QString &conversationKey);
    void EnsureLoaded(int peerUid) { EnsureLoaded(MakePeerConversationKey(peerUid)); }

    const QVector<MessageInfo> &GetHistory(const QString &conversationKey);
    const QVector<MessageInfo> &GetHistory(int peerUid)
    {
        return GetHistory(MakePeerConversationKey(peerUid));
    }

    MessageInfo GetLastMessage(const QString &conversationKey);
    MessageInfo GetLastMessage(int peerUid)
    {
        return GetLastMessage(MakePeerConversationKey(peerUid));
    }

    bool HasConversation(const QString &conversationKey) const
    {
        return _histories.contains(conversationKey);
    }
    bool HasConversation(int peerUid) const
    {
        return HasConversation(MakePeerConversationKey(peerUid));
    }

    QList<QString> GetConversationList();
    QString GetLatestServerMsgId(const QString &conversationKey);

    int GetUnreadCount(const QString &conversationKey) const
    {
        return _unread.value(conversationKey, 0);
    }
    int GetUnreadCount(int peerUid) const
    {
        return GetUnreadCount(MakePeerConversationKey(peerUid));
    }

    int TotalUnread() const;

    void MarkAsRead(const QString &conversationKey);
    void MarkAsRead(int peerUid) { MarkAsRead(MakePeerConversationKey(peerUid)); }

    void SetActiveConversation(const QString &conversationKey)
    {
        _active_conversation_key = conversationKey;
    }
    void SetActivePeer(int peerUid) { SetActiveConversation(MakePeerConversationKey(peerUid)); }

    QString GetActiveConversation() const { return _active_conversation_key; }
    int GetActivePeer() const { return ExtractPeerUidFromConversationKey(_active_conversation_key); }

    void Clear();

signals:
    void sig_message_appended(QString conversationKey, MessageInfo msg);
    void sig_unread_changed(QString conversationKey, int newCount);
    void sig_total_unread_changed(int total);

private:
    ChatMgr();

    QMap<QString, QVector<MessageInfo>> _histories;
    QMap<QString, int> _unread;
    QString _active_conversation_key;
    QSet<QString> _db_loaded;
    QVector<MessageInfo> _empty_vec;
};

#endif // CHATMGR_H
