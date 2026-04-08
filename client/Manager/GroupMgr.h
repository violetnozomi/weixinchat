#ifndef GROUPMGR_H
#define GROUPMGR_H

#include "Singleton.h"

#include <QMap>
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QString>

struct GroupInfoLite {
    QString groupId;
    QString name;
    QString avatarPath;
    QString notice;
    int ownerUid = 0;
    int memberCount = 0;
    QString latestMsgId;
    QMap<int, QString> memberNames;
};

class GroupMgr : public QObject,
                 public Singleton<GroupMgr>,
                 public std::enable_shared_from_this<GroupMgr>
{
    Q_OBJECT
    friend class Singleton<GroupMgr>;

public:
    ~GroupMgr() = default;

    void UpsertGroup(const GroupInfoLite &info);
    bool HasGroup(const QString &groupId) const { return _groups.contains(groupId); }
    GroupInfoLite GetGroup(const QString &groupId) const { return _groups.value(groupId); }

    QString GetGroupName(const QString &groupId) const;
    QString GetGroupAvatarPath(const QString &groupId) const;
    QString ResolveMemberName(const QString &groupId, int uid) const;

    void EnsureGroupInfo(const QString &groupId);
    void MarkLatestMsg(const QString &groupId, const QString &msgId);
    QString GetLatestMsgId(const QString &groupId) const;

    void Clear();

signals:
    void sig_group_updated(QString groupId);

private:
    GroupMgr();

    void UpdateMembers(const QString &groupId, const QJsonArray &members);

    QMap<QString, GroupInfoLite> _groups;
    QSet<QString> _pending_info_requests;
    QSet<QString> _pending_member_requests;
};

#endif // GROUPMGR_H
