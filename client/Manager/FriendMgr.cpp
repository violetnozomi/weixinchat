#include "FriendMgr.h"

// ── 好友列表 ────────────────────────────────────────────────────────────────

void FriendMgr::AddFriend(const FriendInfo &info)
{
    _friends[info.uid] = info;
    emit sig_friend_added(info);
}

void FriendMgr::RemoveFriend(int uid)
{
    if (_friends.remove(uid) > 0)
        emit sig_friend_removed(uid);
}

// ── 好友申请 ─────────────────────────────────────────────────────────────────

void FriendMgr::AddPendingRequest(const FriendRequest &req)
{
    _requests[req.uid] = req;
    emit sig_request_received(req);
    emit sig_pending_count_changed(PendingCount());
}

void FriendMgr::AcceptRequest(int uid)
{
    if (!_requests.contains(uid)) return;
    _requests[uid].status = FriendRequest::Accepted;
    emit sig_request_accepted(uid);
    emit sig_pending_count_changed(PendingCount());
}

void FriendMgr::RejectRequest(int uid)
{
    if (!_requests.contains(uid)) return;
    _requests[uid].status = FriendRequest::Rejected;
    emit sig_request_rejected(uid);
    emit sig_pending_count_changed(PendingCount());
}

bool FriendMgr::HasRequest(int uid) const
{
    return _requests.contains(uid);
}

FriendRequest FriendMgr::GetRequest(int uid) const
{
    return _requests.value(uid);
}

int FriendMgr::PendingCount() const
{
    int count = 0;
    for (const auto &req : _requests)
        if (req.status == FriendRequest::Pending) ++count;
    return count;
}

void FriendMgr::Clear()
{
    _friends.clear();
    _requests.clear();
    _blocked.clear();
}

void FriendMgr::BlockFriend(int uid)
{
    FriendInfo info = _friends.value(uid);
    _friends.remove(uid);
    _blocked[uid] = info;          // 保留资料仕便解除屏蔽时恢复
    emit sig_friend_blocked(uid);
    emit sig_friend_removed(uid);  // UI 同时从联系人列表移除
}

void FriendMgr::UnblockFriend(int uid)
{
    if (!_blocked.contains(uid)) return;
    FriendInfo info = _blocked.take(uid);
    _friends[uid] = info;
    emit sig_friend_added(info);
}
