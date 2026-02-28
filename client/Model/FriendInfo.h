#ifndef FRIENDINFO_H
#define FRIENDINFO_H

#include <QString>

// ── 好友信息（联系人列表条目）─────────────────────────────────────────────
struct FriendInfo {
    int     uid         = 0;
    QString name;
    QString avatarPath;
    QString signature;  // 个性签名
};

#endif // FRIENDINFO_H
