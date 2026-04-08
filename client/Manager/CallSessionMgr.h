#ifndef CALLSESSIONMGR_H
#define CALLSESSIONMGR_H

#include "Singleton.h"

#include <QObject>
#include <QTimer>
#include <memory>

class CallSessionMgr : public QObject,
                       public Singleton<CallSessionMgr>,
                       public std::enable_shared_from_this<CallSessionMgr>
{
    Q_OBJECT
    friend class Singleton<CallSessionMgr>;

public:
    enum class CallState {
        Idle,
        Ringing,
        Accepted,
        Rejected,
        Ended,
        Timeout
    };
    Q_ENUM(CallState)

    enum class CallDirection {
        None,
        Incoming,
        Outgoing
    };
    Q_ENUM(CallDirection)

    ~CallSessionMgr() = default;

    void StartOutgoing(int peerUid, bool withVideo);
    void StartIncoming(int peerUid, bool withVideo);

    void MarkAccepted();
    void MarkRejected();
    void MarkEnded();
    void MarkTimeout();
    void Clear();

    void SetRingingTimeoutMs(int timeoutMs);

    int GetPeerUid() const { return _peer_uid; }
    bool IsWithVideo() const { return _with_video; }
    CallState GetState() const { return _state; }
    CallDirection GetDirection() const { return _direction; }

    bool IsBusy() const;
    bool IsTerminal() const;

signals:
    void sig_call_state_changed(CallSessionMgr::CallState state,
                                int peerUid,
                                bool withVideo,
                                bool outgoing);
    void sig_call_timeout(int peerUid);

private:
    CallSessionMgr();

    void EnterState(CallState state);

private:
    QTimer _ring_timer;
    int _ring_timeout_ms = 30000;

    int _peer_uid = 0;
    bool _with_video = true;
    CallState _state = CallState::Idle;
    CallDirection _direction = CallDirection::None;
};

Q_DECLARE_METATYPE(CallSessionMgr::CallState)
Q_DECLARE_METATYPE(CallSessionMgr::CallDirection)

#endif // CALLSESSIONMGR_H
