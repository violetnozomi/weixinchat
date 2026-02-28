#ifndef FRIENDMGR_H
#define FRIENDMGR_H

#include "Singleton.h"
#include "FriendInfo.h"
#include "FriendRequest.h"
#include <QObject>
#include <QMap>
#include <QList>

// ── 好友列表 & 好友申请的核心业务管理器（单例）─────────────────────────────
// 负责:
//  · 维护好友表 (uid → FriendInfo)
//  · 维护待处理的好友申请列表
//  · 提供查询、增删接口
//  · 发出信号通知 UI 层刷新
class FriendMgr : public QObject, public Singleton<FriendMgr>,
                  public std::enable_shared_from_this<FriendMgr>
{
    Q_OBJECT
    friend class Singleton<FriendMgr>;

public:
    ~FriendMgr() = default;

    // ── 好友列表操作 ────────────────────────────────────────────────────
    void AddFriend(const FriendInfo &info);
    void RemoveFriend(int uid);
    bool HasFriend(int uid) const { return _friends.contains(uid); }
    FriendInfo GetFriend(int uid) const { return _friends.value(uid); }
    QList<FriendInfo> GetAllFriends() const { return _friends.values(); }

    // 屏蔽操作
    void BlockFriend(int uid);          // 屏蔽（从好友表移除，加入屏蔽列表）
    void UnblockFriend(int uid);        // 解除屏蔽
    bool IsBlocked(int uid) const { return _blocked.contains(uid); }
    QList<int> GetBlockedList() const { return _blocked.keys(); }

    // ── 好友申请操作 ────────────────────────────────────────────────────
    void AddPendingRequest(const FriendRequest &req);
    void AcceptRequest(int uid);   // 更新状态为 Accepted
    void RejectRequest(int uid);   // 更新状态为 Rejected
    bool HasRequest(int uid) const;
    FriendRequest GetRequest(int uid) const;
    QList<FriendRequest> GetAllRequests() const { return _requests.values(); }
    int PendingCount() const;

    // ── 全部清空（登出时调用）──────────────────────────────────────────
    void Clear();

signals:
    void sig_friend_added(FriendInfo info);
    void sig_friend_removed(int uid);
    void sig_friend_blocked(int uid);   // 屏蔽好友
    void sig_request_received(FriendRequest req);   // 收到新的好友申请
    void sig_request_accepted(int uid);             // 己方同意某申请
    void sig_request_rejected(int uid);
    void sig_pending_count_changed(int count);

private:
    FriendMgr() = default;

    QMap<int, FriendInfo>    _friends;   // uid → FriendInfo
    QMap<int, FriendRequest> _requests;  // uid → FriendRequest
    QMap<int, FriendInfo>    _blocked;   // uid → FriendInfo (屏蔽列表)
};

#endif // FRIENDMGR_H
