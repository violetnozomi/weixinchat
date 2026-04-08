#include "GroupMgr.h"

#include "FriendMgr.h"
#include "UserMgr.h"
#include "global.h"
#include "tcpmgr.h"

#include <QJsonArray>
#include <QJsonObject>

GroupMgr::GroupMgr()
{
    auto tcp = TcpMgr::GetInstance();

    connect(tcp.get(), &TcpMgr::sig_group_created,
            this, [this](int error, QString groupId, QString groupName,
                         int memberCount, QString /*msg*/) {
        _pending_info_requests.remove(groupId);
        if (error != ErrorCodes::Success || groupId.isEmpty()) {
            return;
        }

        GroupInfoLite info = _groups.value(groupId);
        info.groupId = groupId;
        info.name = groupName;
        info.memberCount = memberCount;
        UpsertGroup(info);
    });

    connect(tcp.get(), &TcpMgr::sig_group_info_received,
            this, [this](int error, QString groupId, QString groupName,
                         QString avatarPath, QString notice,
                         int ownerUid, int memberCount, QString /*msg*/) {
        _pending_info_requests.remove(groupId);
        if (error != ErrorCodes::Success || groupId.isEmpty()) {
            return;
        }

        GroupInfoLite info = _groups.value(groupId);
        info.groupId = groupId;
        info.name = groupName;
        info.avatarPath = avatarPath;
        info.notice = notice;
        info.ownerUid = ownerUid;
        info.memberCount = memberCount;
        UpsertGroup(info);
    });

    connect(tcp.get(), &TcpMgr::sig_group_members_received,
            this, [this](int error, QString groupId, QJsonArray members, QString /*msg*/) {
        _pending_member_requests.remove(groupId);
        if (error != ErrorCodes::Success || groupId.isEmpty()) {
            return;
        }
        UpdateMembers(groupId, members);
        emit sig_group_updated(groupId);
    });

    connect(tcp.get(), &TcpMgr::sig_group_text_msg_received,
            this, [this](QString groupId, int /*senderUid*/, QString /*content*/,
                         qint64 /*timestamp*/, QString msgId) {
        MarkLatestMsg(groupId, msgId);
    });

    connect(tcp.get(), &TcpMgr::sig_group_text_msg_rsp,
            this, [this](int error, QString groupId, QString msgId,
                         qint64 /*timestamp*/, QString /*msg*/) {
        if (error == ErrorCodes::Success) {
            MarkLatestMsg(groupId, msgId);
        }
    });
}

void GroupMgr::UpsertGroup(const GroupInfoLite &info)
{
    if (info.groupId.isEmpty()) {
        return;
    }
    _groups[info.groupId] = info;
    emit sig_group_updated(info.groupId);
}

QString GroupMgr::GetGroupName(const QString &groupId) const
{
    const auto info = _groups.value(groupId);
    if (!info.name.isEmpty()) {
        return info.name;
    }
    return QString::fromUtf8("群聊 %1").arg(groupId);
}

QString GroupMgr::GetGroupAvatarPath(const QString &groupId) const
{
    return _groups.value(groupId).avatarPath;
}

QString GroupMgr::ResolveMemberName(const QString &groupId, int uid) const
{
    if (uid == UserMgr::GetInstance()->GetUid() &&
        !UserMgr::GetInstance()->GetName().isEmpty()) {
        return UserMgr::GetInstance()->GetName();
    }

    const auto info = _groups.value(groupId);
    if (info.memberNames.contains(uid) && !info.memberNames.value(uid).isEmpty()) {
        return info.memberNames.value(uid);
    }

    auto friendMgr = FriendMgr::GetInstance();
    if (friendMgr->HasFriend(uid)) {
        const QString name = friendMgr->GetFriend(uid).name;
        if (!name.isEmpty()) {
            return name;
        }
    }

    return QString::fromUtf8("用户%1").arg(uid);
}

void GroupMgr::EnsureGroupInfo(const QString &groupId)
{
    if (groupId.isEmpty()) {
        return;
    }

    const auto info = _groups.value(groupId);
    if (!info.name.isEmpty() && !info.memberNames.isEmpty()) {
        return;
    }

    auto tcp = TcpMgr::GetInstance();
    if (!_pending_info_requests.contains(groupId)) {
        _pending_info_requests.insert(groupId);
        QJsonObject req;
        req["group_id"] = groupId;
        tcp->SendMsg(ID_GROUP_INFO_REQ, req);
    }
    if (!_pending_member_requests.contains(groupId)) {
        _pending_member_requests.insert(groupId);
        QJsonObject req;
        req["group_id"] = groupId;
        tcp->SendMsg(ID_GROUP_MEMBER_LIST_REQ, req);
    }
}

void GroupMgr::MarkLatestMsg(const QString &groupId, const QString &msgId)
{
    if (groupId.isEmpty() || msgId.isEmpty()) {
        return;
    }
    GroupInfoLite info = _groups.value(groupId);
    info.groupId = groupId;
    info.latestMsgId = msgId;
    _groups[groupId] = info;
}

QString GroupMgr::GetLatestMsgId(const QString &groupId) const
{
    return _groups.value(groupId).latestMsgId;
}

void GroupMgr::Clear()
{
    _groups.clear();
    _pending_info_requests.clear();
    _pending_member_requests.clear();
}

void GroupMgr::UpdateMembers(const QString &groupId, const QJsonArray &members)
{
    GroupInfoLite info = _groups.value(groupId);
    info.groupId = groupId;
    info.memberNames.clear();

    for (const auto &memberValue : members) {
        const QJsonObject member = memberValue.toObject();
        const int uid = member.value("uid").toInt();
        if (uid <= 0) {
            continue;
        }
        info.memberNames[uid] = member.value("name").toString();
    }

    if (info.memberCount <= 0) {
        info.memberCount = info.memberNames.size();
    }
    _groups[groupId] = info;
}
