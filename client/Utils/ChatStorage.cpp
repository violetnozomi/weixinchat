#include "ChatStorage.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QVariant>

// ─────────────────────────────────────────────────────────────────────────────

ChatStorage::ChatStorage() = default;

ChatStorage::~ChatStorage()
{
    Close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

bool ChatStorage::Init(int selfUid)
{
    if (_is_open && _self_uid == selfUid) return true;  // 已经打开过
    if (_is_open) Close();

    _self_uid = selfUid;

    // 数据库文件目录：%AppData%/weixinchat  (Windows)
    //                  ~/.local/share/weixinchat (Linux)
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);

    QString dbPath = dataDir + QString("/chat_%1.db").arg(selfUid);
    _connection_name = QString("chat_db_%1").arg(selfUid);

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

    // 启动时清理超过 365 天的本地消息
    int removed = DeleteOldMessages(365);
    if (removed > 0)
        qDebug() << "[ChatStorage] Cleaned up" << removed << "old local messages";

    qDebug() << "[ChatStorage] Init OK for uid=" << selfUid;
    return true;
}

void ChatStorage::Close()
{
    if (!_is_open) return;
    _db.close();
    QSqlDatabase::removeDatabase(_connection_name);
    _is_open = false;
    _self_uid = -1;
    qDebug() << "[ChatStorage] Closed";
}

// ─────────────────────────────────────────────────────────────────────────────
// Schema
// ─────────────────────────────────────────────────────────────────────────────

bool ChatStorage::createSchema()
{
    QSqlQuery q(_db);
    // messages 表：存储所有聊天记录
    // peer_uid = 会话的对方 uid（不管消息方向，都用同一个 peer_uid 归档）
    bool ok = q.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            peer_uid    INTEGER NOT NULL,
            from_uid    INTEGER NOT NULL,
            to_uid      INTEGER NOT NULL,
            content     TEXT    NOT NULL,
            timestamp   INTEGER NOT NULL,   -- Unix秒
            is_self     INTEGER NOT NULL,   -- 1=自己发的, 0=对方发的
            type        INTEGER NOT NULL DEFAULT 0  -- 0=文本
        )
    )");
    if (!ok) {
        qWarning() << "[ChatStorage] createSchema error:" << q.lastError().text();
        return false;
    }
    // 按 peer_uid + timestamp 的联合索引，加速历史加载
    q.exec("CREATE INDEX IF NOT EXISTS idx_peer_time ON messages(peer_uid, timestamp)");
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

// ─────────────────────────────────────────────────────────────────────────────
// SaveMessage
// ─────────────────────────────────────────────────────────────────────────────

bool ChatStorage::SaveMessage(const MessageInfo &msg)
{
    if (!ensureOpen()) return false;

    // 确定 peer_uid
    const int peerUid = msg.isSelf ? msg.toUid : msg.fromUid;

    // 如果 time 为空，使用当前时刻
    qint64 ts = msg.time.isValid()
                ? msg.time.toSecsSinceEpoch()
                : QDateTime::currentSecsSinceEpoch();

    QSqlQuery q(_db);
    q.prepare(
        "INSERT INTO messages(peer_uid, from_uid, to_uid, content, timestamp, is_self, type)"
        " VALUES(:peer, :from, :to, :content, :ts, :self, :type)"
    );
    q.bindValue(":peer",    peerUid);
    q.bindValue(":from",    msg.fromUid);
    q.bindValue(":to",      msg.toUid);
    q.bindValue(":content", msg.content);
    q.bindValue(":ts",      ts);
    q.bindValue(":self",    msg.isSelf ? 1 : 0);
    q.bindValue(":type",    static_cast<int>(msg.type));

    if (!q.exec()) {
        qWarning() << "[ChatStorage] SaveMessage error:" << q.lastError().text();
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadHistory
// ─────────────────────────────────────────────────────────────────────────────

QVector<MessageInfo> ChatStorage::LoadHistory(int peerUid, int limit)
{
    QVector<MessageInfo> result;
    if (!ensureOpen()) return result;

    QSqlQuery q(_db);
    // 先取最近 limit 条（倒序），再反转为正序
    q.prepare(
        "SELECT from_uid, to_uid, content, timestamp, is_self, type"
        " FROM messages"
        " WHERE peer_uid = :peer"
        " ORDER BY timestamp DESC, id DESC"
        " LIMIT :lim"
    );
    q.bindValue(":peer", peerUid);
    q.bindValue(":lim",  limit);

    if (!q.exec()) {
        qWarning() << "[ChatStorage] LoadHistory error:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        MessageInfo m;
        m.fromUid = q.value(0).toInt();
        m.toUid   = q.value(1).toInt();
        m.content = q.value(2).toString();
        m.time    = QDateTime::fromSecsSinceEpoch(q.value(3).toLongLong());
        m.isSelf  = q.value(4).toInt() == 1;
        m.type    = static_cast<MessageInfo::Type>(q.value(5).toInt());
        result.prepend(m);   // 反转为正序（最早的在最前）
    }

    qDebug() << "[ChatStorage] Loaded" << result.size()
             << "messages for peer=" << peerUid;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// DeleteOldMessages
// ─────────────────────────────────────────────────────────────────────────────

int ChatStorage::DeleteOldMessages(int keepDays)
{
    if (!ensureOpen()) return 0;

    qint64 cutoff = QDateTime::currentSecsSinceEpoch()
                    - static_cast<qint64>(keepDays) * 86400;

    QSqlQuery q(_db);
    q.prepare("DELETE FROM messages WHERE timestamp < :cutoff");
    q.bindValue(":cutoff", cutoff);

    if (!q.exec()) {
        qWarning() << "[ChatStorage] DeleteOldMessages error:" << q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

// ─────────────────────────────────────────────────────────────────────────────
// GetConversationUids
// ─────────────────────────────────────────────────────────────────────────────

QList<int> ChatStorage::GetConversationUids()
{
    QList<int> uids;
    if (!ensureOpen()) return uids;

    QSqlQuery q(_db);
    // 每个 peer_uid 取最新时间戳，按最近会话排列
    bool ok = q.exec(
        "SELECT peer_uid FROM messages"
        " GROUP BY peer_uid"
        " ORDER BY MAX(timestamp) DESC"
    );
    if (!ok) {
        qWarning() << "[ChatStorage] GetConversationUids error:" << q.lastError().text();
        return uids;
    }
    while (q.next())
        uids.append(q.value(0).toInt());

    return uids;
}
