#include "CallSessionMgr.h"

#include <QDebug>

CallSessionMgr::CallSessionMgr() {
    qRegisterMetaType<CallSessionMgr::CallState>("CallSessionMgr::CallState");
    qRegisterMetaType<CallSessionMgr::CallDirection>("CallSessionMgr::CallDirection");
    _ring_timer.setSingleShot(true);
    connect(&_ring_timer, &QTimer::timeout, this, [this]() {
        if (_state != CallState::Ringing) {
            return;
        }
        const int peer_uid = _peer_uid;
        MarkTimeout();
        emit sig_call_timeout(peer_uid);
    });
}

void CallSessionMgr::StartOutgoing(int peerUid, bool withVideo) {
    if (peerUid <= 0) {
        qWarning() << "[CallSession] invalid outgoing peer uid:" << peerUid;
        return;
    }

    _peer_uid = peerUid;
    _with_video = withVideo;
    _direction = CallDirection::Outgoing;
    EnterState(CallState::Ringing);
}

void CallSessionMgr::StartIncoming(int peerUid, bool withVideo) {
    if (peerUid <= 0) {
        qWarning() << "[CallSession] invalid incoming peer uid:" << peerUid;
        return;
    }

    _peer_uid = peerUid;
    _with_video = withVideo;
    _direction = CallDirection::Incoming;
    EnterState(CallState::Ringing);
}

void CallSessionMgr::MarkAccepted() {
    if (_state != CallState::Ringing) {
        return;
    }
    EnterState(CallState::Accepted);
}

void CallSessionMgr::MarkRejected() {
    if (_state == CallState::Idle || IsTerminal()) {
        return;
    }
    EnterState(CallState::Rejected);
}

void CallSessionMgr::MarkEnded() {
    if (_state == CallState::Idle || IsTerminal()) {
        return;
    }
    EnterState(CallState::Ended);
}

void CallSessionMgr::MarkTimeout() {
    if (_state != CallState::Ringing) {
        return;
    }
    EnterState(CallState::Timeout);
}

void CallSessionMgr::Clear() {
    const bool changed =
        _state != CallState::Idle || _peer_uid != 0 || _direction != CallDirection::None;

    _ring_timer.stop();
    _peer_uid = 0;
    _with_video = true;
    _state = CallState::Idle;
    _direction = CallDirection::None;

    if (changed) {
        emit sig_call_state_changed(_state, _peer_uid, _with_video, false);
    }
}

void CallSessionMgr::SetRingingTimeoutMs(int timeoutMs) {
    if (timeoutMs < 1000) {
        timeoutMs = 1000;
    }
    _ring_timeout_ms = timeoutMs;
}

bool CallSessionMgr::IsBusy() const {
    return _state == CallState::Ringing || _state == CallState::Accepted;
}

bool CallSessionMgr::IsTerminal() const {
    return _state == CallState::Rejected ||
           _state == CallState::Ended ||
           _state == CallState::Timeout;
}

void CallSessionMgr::EnterState(CallState state) {
    if (_state == state) {
        return;
    }

    _state = state;
    if (_state == CallState::Ringing) {
        _ring_timer.start(_ring_timeout_ms);
    } else {
        _ring_timer.stop();
    }

    emit sig_call_state_changed(_state,
                                _peer_uid,
                                _with_video,
                                _direction == CallDirection::Outgoing);
}
