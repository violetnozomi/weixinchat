#ifndef MESSAGEINFO_H
#define MESSAGEINFO_H

#include <QString>
#include <QDateTime>

// ── 单条消息数据模型 ──────────────────────────────────────────────────────
struct MessageInfo {
    int      msgId      = 0;
    int      fromUid    = 0;
    int      toUid      = 0;
    QString  content;
    QDateTime time;
    bool     isSelf     = false;   // true = 自己发送的
    enum Type { Text, Image, File } type = Text;
};

#endif // MESSAGEINFO_H
