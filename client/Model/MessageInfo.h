#ifndef MESSAGEINFO_H
#define MESSAGEINFO_H

#include <QDateTime>
#include <QString>

enum class ConversationType {
    Peer = 0,
    Group = 1,
};

inline QString MakePeerConversationKey(int uid)
{
    return QString("u:%1").arg(uid);
}

inline QString MakeGroupConversationKey(const QString &groupId)
{
    return QString("g:%1").arg(groupId);
}

inline bool IsGroupConversationKey(const QString &conversationKey)
{
    return conversationKey.startsWith("g:");
}

inline QString ExtractGroupIdFromConversationKey(const QString &conversationKey)
{
    return IsGroupConversationKey(conversationKey)
        ? conversationKey.mid(2)
        : QString();
}

inline int ExtractPeerUidFromConversationKey(const QString &conversationKey)
{
    if (conversationKey.startsWith("u:")) {
        return conversationKey.mid(2).toInt();
    }
    if (!conversationKey.isEmpty() && !IsGroupConversationKey(conversationKey)) {
        return conversationKey.toInt();
    }
    return 0;
}

inline bool IsValidPeerUid(int uid)
{
    return uid > 0;
}

inline bool IsValidConversationKey(const QString &conversationKey)
{
    if (conversationKey.isEmpty()) {
        return false;
    }
    if (IsGroupConversationKey(conversationKey)) {
        return !ExtractGroupIdFromConversationKey(conversationKey).isEmpty();
    }
    return IsValidPeerUid(ExtractPeerUidFromConversationKey(conversationKey));
}

struct MessageInfo {
    QString msgId = "";
    QString conversationKey = "";
    ConversationType conversationType = ConversationType::Peer;
    QString groupId = "";
    int fromUid = 0;
    int toUid = 0;
    QString content = "";
    QDateTime time;
    bool isSelf = false;

    enum Type {
        Text,
        Image,
        File,
        Voice
    } type = Text;
};

#endif // MESSAGEINFO_H
