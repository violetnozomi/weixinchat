#ifndef FRIENDREQUEST_H
#define FRIENDREQUEST_H

#include <QString>
#include <QDateTime>

// ── 好友申请数据模型 ──────────────────────────────────────────────────────
struct FriendRequest {
    int      uid        = 0;
    QString  name;
    QString  message;           // 验证留言
    QString  avatarPath;
    QDateTime time;
    enum Status { Pending, Accepted, Rejected } status = Pending;
};

#endif // FRIENDREQUEST_H
