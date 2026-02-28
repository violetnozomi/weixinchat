#ifndef USERINFO_H
#define USERINFO_H

#include <QString>

// ── 搜索结果 / 好友候选用户信息 ────────────────────────────────────────────
struct UserInfo {
    int     uid         = 0;
    QString name;
    QString avatarPath;
    QString email;
    int     sex         = 0;   // 0=未知 1=男 2=女
};

// ── 当前登录用户的会话信息 ────────────────────────────────────────────────
struct SelfInfo {
    int     uid         = 0;
    QString name;
    QString avatarPath;    QString signature;    QString token;
    QString host;       // ChatServer host
    QString port;       // ChatServer port
};

#endif // USERINFO_H
