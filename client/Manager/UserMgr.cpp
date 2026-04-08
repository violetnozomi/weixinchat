#include "UserMgr.h"
#include "CallSessionMgr.h"
#include "GroupMgr.h"

void UserMgr::SetSelfInfo(int uid, const QString &name,
                           const QString &token,
                           const QString &host, const QString &port,
                           const QString &avatarPath,
                           const QString &signature)
{
    _self.uid        = uid;
    _self.name       = name;
    _self.token      = token;
    _self.host       = host;
    _self.port       = port;
    _self.avatarPath = avatarPath;
    _self.signature  = signature;
    emit sig_login_success();
}

void UserMgr::UpdateProfile(const QString &name,
                              const QString &signature,
                              const QString &avatarPath)
{
    if (!name.isEmpty())       _self.name       = name;
    if (!signature.isNull())   _self.signature  = signature;
    if (!avatarPath.isEmpty()) _self.avatarPath = avatarPath;
    emit sig_profile_updated();
}

void UserMgr::Clear()
{
    CallSessionMgr::GetInstance()->Clear();
    GroupMgr::GetInstance()->Clear();
    _self = SelfInfo{};
    emit sig_logout();
}
