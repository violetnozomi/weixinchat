#ifndef CHATSTORAGE_H
#define CHATSTORAGE_H

#include "MessageInfo.h"
#include "Singleton.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

class ChatStorage : public Singleton<ChatStorage>
{
    friend class Singleton<ChatStorage>;

public:
    ~ChatStorage();

    bool Init(int selfUid);
    void Close();

    bool IsOpen() const { return _is_open; }

    bool SaveMessage(const MessageInfo &msg);
    bool UpdatePendingGroupTextAck(const QString &conversationKey,
                                   const QString &serverMsgId,
                                   const QDateTime &serverTime);
    QString GetLatestServerMsgId(const QString &conversationKey);
    QVector<MessageInfo> LoadHistory(const QString &conversationKey, int limit = 500);
    QVector<MessageInfo> LoadHistory(int peerUid, int limit = 500)
    {
        return LoadHistory(MakePeerConversationKey(peerUid), limit);
    }

    int DeleteOldMessages(int keepDays = 365);
    QList<QString> GetConversationKeys();

private:
    ChatStorage();

    bool createSchema();
    bool ensureOpen() const;

    QSqlDatabase _db;
    QString _connection_name;
    bool _is_open = false;
    int _self_uid = -1;
};

#endif // CHATSTORAGE_H
