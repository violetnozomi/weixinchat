#include "ChatStorage.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {

constexpr const char *kMessagesTable = "messages_v2";

bool TableExists(QSqlDatabase &db, const QString &tableName)
{
    QSqlQuery q(db);
    q.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=:name");
    q.bindValue(":name", tableName);
    return q.exec() && q.next();
}

int TableRowCount(QSqlDatabase &db, const QString &tableName)
{
    QSqlQuery q(db);
    if (!q.exec(QString("SELECT COUNT(*) FROM %1").arg(tableName))) {
        return 0;
    }
    return q.next() ? q.value(0).toInt() : 0;
}

QString BuildConversationKeyForStorage(const MessageInfo &msg)
{
    if (!msg.conversationKey.isEmpty()) {
        return msg.conversationKey;
    }
    if (msg.conversationType == ConversationType::Group || !msg.groupId.isEmpty()) {
        return MakeGroupConversationKey(msg.groupId);
    }

    const int peerUid = msg.isSelf ? msg.toUid : msg.fromUid;
    return MakePeerConversationKey(peerUid);
}

} // namespace

ChatStorage::ChatStorage() = default;

ChatStorage::~ChatStorage()
{
    Close();
}

bool ChatStorage::Init(int selfUid)
{
    if (_is_open && _self_uid == selfUid) {
        return true;
    }
    if (_is_open) {
        Close();
    }

    _self_uid = selfUid;

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);

    const QString dbPath = dataDir + QString("/chat_%1.db").arg(selfUid);
    _connection_name = QString("chat_db_%1").arg(selfUid);

    // 先移除同名连接（若存在），避免 addDatabase 警告
    if (QSqlDatabase::contains(_connection_name)) {
        QSqlDatabase::removeDatabase(_connection_name);
    }

    qDebug() << "[ChatStorage] Opening DB:" << dbPath;

    _db = QSqlDatabase::addDatabase("QSQLITE", _connection_name);
    _db.setDatabaseName(dbPath);

    if (!_db.open()) {
        qWarning() << "[ChatStorage] Failed to open DB:" << _db.lastError().text();
        return false;
    }

    if (!createSchema()) {
        qWarning() << "[ChatStorage] Failed to create schema";
        _db.close();
        return false;
    }

    _is_open = true;

    const int removed = DeleteOldMessages(365);
    if (removed > 0) {
        qDebug() << "[ChatStorage] Cleaned up" << removed << "old local messages";
    }

    qDebug() << "[ChatStorage] Init OK for uid=" << selfUid;
    return true;
}

void ChatStorage::Close()
{
    if (!_is_open) {
        return;
    }

    _db.close();
    QSqlDatabase::removeDatabase(_connection_name);
    _is_open = false;
    _self_uid = -1;
    qDebug() << "[ChatStorage] Closed";
}

bool ChatStorage::createSchema()
{
    QSqlQuery q(_db);
    const bool ok = q.exec(QString(R"(
        CREATE TABLE IF NOT EXISTS %1 (
            id                INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_key  TEXT    NOT NULL,
            conversation_type INTEGER NOT NULL DEFAULT 0,
            group_id          TEXT    NOT NULL DEFAULT '',
            server_msg_id     TEXT    NOT NULL DEFAULT '',
            from_uid          INTEGER NOT NULL,
            to_uid            INTEGER NOT NULL DEFAULT 0,
            content           TEXT    NOT NULL,
            timestamp         INTEGER NOT NULL,
            is_self           INTEGER NOT NULL,
            type              INTEGER NOT NULL DEFAULT 0
        )
    )").arg(kMessagesTable));
    if (!ok) {
        qWarning() << "[ChatStorage] createSchema error:" << q.lastError().text();
        return false;
    }

    q.exec(QString(
        "CREATE INDEX IF NOT EXISTS idx_messages_v2_conv_time "
        "ON %1(conversation_key, timestamp)").arg(kMessagesTable));

    if (TableExists(_db, "messages") && TableRowCount(_db, kMessagesTable) == 0) {
        QSqlQuery migrate(_db);
        const bool migrated = migrate.exec(QString(R"(
            INSERT INTO %1(
                conversation_key, conversation_type, group_id, server_msg_id,
                from_uid, to_uid, content, timestamp, is_self, type
            )
            SELECT
                'u:' || peer_uid,
                0,
                '',
                '',
                from_uid,
                to_uid,
                content,
                timestamp,
                is_self,
                type
            FROM messages
        )").arg(kMessagesTable));
        if (!migrated) {
            qWarning() << "[ChatStorage] migrate legacy messages error:"
                       << migrate.lastError().text();
        }
    }

    return true;
}

bool ChatStorage::ensureOpen() const
{
    if (!_is_open) {
        qWarning() << "[ChatStorage] DB not open, call Init() first";
        return false;
    }
    return true;
}

bool ChatStorage::SaveMessage(const MessageInfo &msg)
{
    if (!ensureOpen()) {
        return false;
    }

    const QString conversationKey = BuildConversationKeyForStorage(msg);
    const ConversationType conversationType =
        IsGroupConversationKey(conversationKey) ? ConversationType::Group : ConversationType::Peer;
    const QString groupId = msg.groupId.isNull() ? QString("") : msg.groupId;
    const QString serverMsgId = msg.msgId.isNull() ? QString("") : msg.msgId;
    const QString content = msg.content.isNull() ? QString("") : msg.content;
    const qint64 ts = msg.time.isValid()
        ? msg.time.toSecsSinceEpoch()
        : QDateTime::currentSecsSinceEpoch();

    QSqlQuery q(_db);
    q.prepare(QString(
        "INSERT INTO %1("
        " conversation_key, conversation_type, group_id, server_msg_id,"
        " from_uid, to_uid, content, timestamp, is_self, type)"
        " VALUES(:conversation_key, :conversation_type, :group_id, :server_msg_id,"
        " :from_uid, :to_uid, :content, :timestamp, :is_self, :type)"
    ).arg(kMessagesTable));
    q.bindValue(":conversation_key", conversationKey);
    q.bindValue(":conversation_type", static_cast<int>(conversationType));
    q.bindValue(":group_id", groupId);
    q.bindValue(":server_msg_id", serverMsgId);
    q.bindValue(":from_uid", msg.fromUid);
    q.bindValue(":to_uid", msg.toUid);
    q.bindValue(":content", content);
    q.bindValue(":timestamp", ts);
    q.bindValue(":is_self", msg.isSelf ? 1 : 0);
    q.bindValue(":type", static_cast<int>(msg.type));

    if (!q.exec()) {
        qWarning() << "[ChatStorage] SaveMessage error:" << q.lastError().text();
        return false;
    }

    return true;
}

bool ChatStorage::UpdatePendingGroupTextAck(const QString &conversationKey,
                                            const QString &serverMsgId,
                                            const QDateTime &serverTime)
{
    if (!ensureOpen() || conversationKey.isEmpty() || serverMsgId.isEmpty()) {
        return false;
    }

    const qint64 ts = serverTime.isValid()
        ? serverTime.toSecsSinceEpoch()
        : QDateTime::currentSecsSinceEpoch();

    QSqlQuery q(_db);
    q.prepare(QString(R"(
        UPDATE %1
        SET server_msg_id = :server_msg_id,
            timestamp = :timestamp
        WHERE id = (
            SELECT id
            FROM %1
            WHERE conversation_key = :conversation_key
              AND conversation_type = :conversation_type
              AND is_self = 1
              AND type = :type
              AND server_msg_id = ''
            ORDER BY timestamp ASC, id ASC
            LIMIT 1
        )
    )").arg(kMessagesTable));
    q.bindValue(":server_msg_id", serverMsgId);
    q.bindValue(":timestamp", ts);
    q.bindValue(":conversation_key", conversationKey);
    q.bindValue(":conversation_type", static_cast<int>(ConversationType::Group));
    q.bindValue(":type", static_cast<int>(MessageInfo::Text));

    if (!q.exec()) {
        qWarning() << "[ChatStorage] UpdatePendingGroupTextAck error:" << q.lastError().text();
        return false;
    }

    return q.numRowsAffected() > 0;
}

QString ChatStorage::GetLatestServerMsgId(const QString &conversationKey)
{
    if (!ensureOpen() || conversationKey.isEmpty()) {
        return QString();
    }

    QSqlQuery q(_db);
    q.prepare(QString(
        "SELECT server_msg_id FROM %1 "
        "WHERE conversation_key = :conversation_key AND server_msg_id <> '' "
        "ORDER BY timestamp DESC, id DESC LIMIT 1"
    ).arg(kMessagesTable));
    q.bindValue(":conversation_key", conversationKey);

    if (!q.exec()) {
        qWarning() << "[ChatStorage] GetLatestServerMsgId error:" << q.lastError().text();
        return QString();
    }

    if (!q.next()) {
        return QString();
    }
    return q.value(0).toString();
}

QVector<MessageInfo> ChatStorage::LoadHistory(const QString &conversationKey, int limit)
{
    QVector<MessageInfo> result;
    if (!ensureOpen() || !IsValidConversationKey(conversationKey)) {
        return result;
    }
    limit = qBound(1, limit, 5000);

    QSqlQuery q(_db);
    q.prepare(QString(
        "SELECT conversation_type, group_id, server_msg_id, from_uid, to_uid,"
        " content, timestamp, is_self, type"
        " FROM %1"
        " WHERE conversation_key = :conversation_key"
        " ORDER BY timestamp DESC, id DESC"
        " LIMIT :limit"
    ).arg(kMessagesTable));
    q.bindValue(":conversation_key", conversationKey);
    q.bindValue(":limit", limit);

    if (!q.exec()) {
        qWarning() << "[ChatStorage] LoadHistory error:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        MessageInfo msg;
        msg.conversationKey = conversationKey;
        msg.conversationType = static_cast<ConversationType>(q.value(0).toInt());
        msg.groupId = q.value(1).toString();
        msg.msgId = q.value(2).toString();
        msg.fromUid = q.value(3).toInt();
        msg.toUid = q.value(4).toInt();
        msg.content = q.value(5).toString();
        msg.time = QDateTime::fromSecsSinceEpoch(q.value(6).toLongLong());
        msg.isSelf = q.value(7).toInt() == 1;
        msg.type = static_cast<MessageInfo::Type>(q.value(8).toInt());
        result.prepend(msg);
    }

    qDebug() << "[ChatStorage] Loaded" << result.size()
             << "messages for conversation=" << conversationKey;
    return result;
}

int ChatStorage::DeleteOldMessages(int keepDays)
{
    if (!ensureOpen()) {
        return 0;
    }

    const qint64 cutoff = QDateTime::currentSecsSinceEpoch()
        - static_cast<qint64>(keepDays) * 86400;

    QSqlQuery q(_db);
    q.prepare(QString("DELETE FROM %1 WHERE timestamp < :cutoff").arg(kMessagesTable));
    q.bindValue(":cutoff", cutoff);

    if (!q.exec()) {
        qWarning() << "[ChatStorage] DeleteOldMessages error:" << q.lastError().text();
        return 0;
    }

    return q.numRowsAffected();
}

QList<QString> ChatStorage::GetConversationKeys()
{
    QList<QString> keys;
    if (!ensureOpen()) {
        return keys;
    }

    QSqlQuery q(_db);
    const bool ok = q.exec(QString(
        "SELECT conversation_key"
        " FROM %1"
        " GROUP BY conversation_key"
        " ORDER BY MAX(timestamp) DESC"
    ).arg(kMessagesTable));
    if (!ok) {
        qWarning() << "[ChatStorage] GetConversationKeys error:" << q.lastError().text();
        return keys;
    }

    QList<QString> invalidKeys;
    while (q.next()) {
        const QString conversationKey = q.value(0).toString();
        if (!IsValidConversationKey(conversationKey)) {
            invalidKeys.append(conversationKey);
            continue;
        }
        keys.append(conversationKey);
    }

    for (const QString &conversationKey : invalidKeys) {
        QSqlQuery cleanup(_db);
        cleanup.prepare(QString("DELETE FROM %1 WHERE conversation_key = :conversation_key")
                            .arg(kMessagesTable));
        cleanup.bindValue(":conversation_key", conversationKey);
        if (!cleanup.exec()) {
            qWarning() << "[ChatStorage] cleanup invalid conversation error:"
                       << conversationKey << cleanup.lastError().text();
        } else {
            qWarning() << "[ChatStorage] removed invalid conversation:" << conversationKey;
        }
    }

    return keys;
}
