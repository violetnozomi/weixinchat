// client/Manager/webrtcmgr.cpp
#include "webrtcmgr.h"
#include "tcpmgr.h"
#include "UserMgr.h"
#include "cameracapturer.h"
#include "global.h"

#include <QCoreApplication>
#include <QDebug>
#include <thread>

WebRTCMgr& WebRTCMgr::GetInstance() {
    static WebRTCMgr instance;
    return instance;
}

WebRTCMgr::WebRTCMgr() {}

WebRTCMgr::~WebRTCMgr() {
    HangUp();
}

void WebRTCMgr::RunOnSignalingThread(std::function<void()> task) {
    if (!task) return;
    if (!_signaling_thread) {
        qWarning() << "[WebRTC] signaling thread not ready";
        return;
    }
    _signaling_thread->PostTask(std::move(task));
}

bool WebRTCMgr::Init() {
    static bool inited = false;
    if (inited) return true;
    inited = true;

    webrtc::InitializeSSL();

    _signaling_thread = webrtc::Thread::Create();
    _signaling_thread->Start();

    _network_thread = webrtc::Thread::CreateWithSocketServer();
    _network_thread->Start();

    _worker_thread = webrtc::Thread::Create();
    _worker_thread->Start();

    std::thread([this]() {
        webrtc::ThreadManager::Instance()->WrapCurrentThread();

        _peer_connection_factory = webrtc::CreatePeerConnectionFactory(
            _network_thread.get(),
            _worker_thread.get(),
            _signaling_thread.get(),
            nullptr,
            webrtc::CreateBuiltinAudioEncoderFactory(),
            webrtc::CreateBuiltinAudioDecoderFactory(),
            webrtc::CreateBuiltinVideoEncoderFactory(),
            webrtc::CreateBuiltinVideoDecoderFactory(),
            nullptr,
            nullptr);

        if (_peer_connection_factory) qDebug() << "[WebRTC] factory ready";
        else qDebug() << "[WebRTC] factory failed";

        webrtc::Thread::Current()->Run();
    }).detach();

    return true;
}

void WebRTCMgr::StartCall(int targetUid, bool withVideo) {
    RunOnSignalingThread([this, targetUid, withVideo]() {
        _target_uid = targetUid;
        _with_video = withVideo;

        if (!CreatePeerConnection()) return;
        _peer_connection->CreateOffer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    });
}

void WebRTCMgr::acceptCall() {
    RunOnSignalingThread([this]() {
        if (!_peer_connection) return;
        _peer_connection->CreateAnswer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    });
}

void WebRTCMgr::HandleRemoteOffer(int fromUid, const QString& sdp) {
    RunOnSignalingThread([this, fromUid, sdp]() {
        _target_uid = fromUid;

        if (!CreatePeerConnection()) return;

        webrtc::SdpParseError error;
        auto remote = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp.toStdString(), &error);
        if (!remote) {
            qWarning() << "[WebRTC] parse offer failed:" << error.description.c_str();
            return;
        }

        const bool withVideo = sdp.toStdString().find("m=video") != std::string::npos;
        _with_video = withVideo;

        _peer_connection->SetRemoteDescription(
            DummySetSessionDescriptionObserver::Create().get(),
            remote.release());

        QMetaObject::invokeMethod(this, [this, fromUid, withVideo]() {
            emit sig_incomingCall(fromUid, withVideo);
        }, Qt::QueuedConnection);
    });
}

void WebRTCMgr::HandleRemoteAnswer(int /*fromUid*/, const QString& sdp) {
    RunOnSignalingThread([this, sdp]() {
        if (!_peer_connection) return;

        webrtc::SdpParseError error;
        auto remote = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp.toStdString(), &error);
        if (!remote) {
            qWarning() << "[WebRTC] parse answer failed";
            return;
        }

        _peer_connection->SetRemoteDescription(
            DummySetSessionDescriptionObserver::Create().get(),
            remote.release());
    });
}

void WebRTCMgr::HandleRemoteIceCandidate(int /*fromUid*/, const QString& sdp_mid, int sdp_mline_index, const QString& candidate) {
    RunOnSignalingThread([this, sdp_mid, sdp_mline_index, candidate]() {
        if (!_peer_connection) return;

        webrtc::SdpParseError error;
        auto ice = webrtc::CreateIceCandidate(
            sdp_mid.toStdString(), sdp_mline_index, candidate.toStdString(), &error);
        if (!ice) {
            qWarning() << "[WebRTC] parse ice failed";
            return;
        }

        _peer_connection->AddIceCandidate(ice);
    });
}

void WebRTCMgr::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
    RunOnSignalingThread([this, desc]() {
        if (!_peer_connection || !desc) return;

        _peer_connection->SetLocalDescription(DummySetSessionDescriptionObserver::Create().get(), desc);

        std::string sdp;
        desc->ToString(&sdp);

        QJsonObject payload;
        payload["sdp"] = QString::fromStdString(sdp);

        QJsonObject msg;
        msg["type"] = (desc->GetType() == webrtc::SdpType::kOffer) ? "webrtc_offer" : "webrtc_answer";
        msg["from_uid"] = UserMgr::GetInstance()->GetUid();
        msg["to_uid"] = _target_uid;
        msg["payload"] = payload;

        QMetaObject::invokeMethod(QCoreApplication::instance(), [msg]() {
            TcpMgr::GetInstance()->SendMsg(ID_WEBRTC_SIGNALING, msg);
        }, Qt::QueuedConnection);
    });
}

void WebRTCMgr::OnFailure(webrtc::RTCError error) {
    qWarning() << "[WebRTC] create sdp failed:" << error.message();
}

void WebRTCMgr::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    std::string cand;
    if (!candidate->ToString(&cand)) return;

    QJsonObject payload;
    payload["candidate"] = QString::fromStdString(cand);
    payload["sdpMid"] = QString::fromStdString(candidate->sdp_mid());
    payload["sdpMLineIndex"] = candidate->sdp_mline_index();

    SendSignalingMessage("webrtc_ice", payload);
}

void WebRTCMgr::OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
    auto track = transceiver->receiver()->track();
    if (!track) return;

    if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
        _remote_video_track = static_cast<webrtc::VideoTrackInterface*>(track.get());
        if (_remote_video_track && _remote_sink) {
            _remote_video_track->AddOrUpdateSink(_remote_sink, webrtc::VideoSinkWants());
        }
    } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
        qDebug() << "[WebRTC] remote audio track arrived";
    }
}

bool WebRTCMgr::CreatePeerConnection() {
    if (_peer_connection) return true;
    if (!_peer_connection_factory) return false;

    webrtc::PeerConnectionInterface::RTCConfiguration cfg;
    webrtc::PeerConnectionInterface::IceServer server;
    server.uri = "stun:stun.l.google.com:19302";
    cfg.servers.push_back(server);

    webrtc::PeerConnectionDependencies deps(this);
    auto result = _peer_connection_factory->CreatePeerConnectionOrError(cfg, std::move(deps));
    if (!result.ok()) return false;

    _peer_connection = result.MoveValue();

    // 音频轨始终添加（语音通话基础）
    auto audioSource = _peer_connection_factory->CreateAudioSource(cricket::AudioOptions());
    local_audio_track_ = _peer_connection_factory->CreateAudioTrack("local_audio_track", audioSource);
    if (local_audio_track_) {
        _peer_connection->AddTrack(local_audio_track_, {"stream_id"});
    }

    // 视频轨按模式添加
    if (_with_video && local_video_track_) {
        _peer_connection->AddTrack(local_video_track_, {"stream_id"});
    }

    return true;
}

void WebRTCMgr::SendSignalingMessage(const QString& type, const QJsonObject& payload) {
    if (_target_uid == 0) return;

    QJsonObject msg;
    msg["from_uid"] = UserMgr::GetInstance()->GetUid();
    msg["to_uid"] = _target_uid;
    msg["type"] = type;
    msg["payload"] = payload;

    QMetaObject::invokeMethod(QCoreApplication::instance(), [msg]() {
        TcpMgr::GetInstance()->SendMsg(ID_WEBRTC_SIGNALING, msg);
    }, Qt::QueuedConnection);
}

void WebRTCMgr::startLocalVideo(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
    RunOnSignalingThread([this, sink]() {
        if (!_peer_connection_factory) return;

        auto source = CapturerTrackSource::Create();
        if (!source) {
            qWarning() << "[WebRTC] create local video source failed";
            return;
        }

        local_video_track_ = _peer_connection_factory->CreateVideoTrack(source, "local_video_track");
        if (local_video_track_ && sink) {
            local_video_track_->AddOrUpdateSink(sink, webrtc::VideoSinkWants());
        }

        // 若 PC 已存在且是视频模式，可补加
        if (_peer_connection && _with_video && local_video_track_) {
            _peer_connection->AddTrack(local_video_track_, {"stream_id"});
        }
    });
}

void WebRTCMgr::stopLocalVideo(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
    RunOnSignalingThread([this, sink]() {
        if (local_video_track_ && sink) {
            local_video_track_->RemoveSink(sink);
        }
        local_video_track_ = nullptr;
    });
}

void WebRTCMgr::SetLocalVideoEnabled(bool enabled) {
    RunOnSignalingThread([this, enabled]() {
        if (local_video_track_) {
            local_video_track_->set_enabled(enabled);
        }
    });
}

void WebRTCMgr::setRemoteSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
    RunOnSignalingThread([this, sink]() {
        _remote_sink = sink;
        if (_remote_video_track && _remote_sink) {
            _remote_video_track->AddOrUpdateSink(_remote_sink, webrtc::VideoSinkWants());
        }
    });
}

void WebRTCMgr::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
    switch (new_state) {
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
        QMetaObject::invokeMethod(this, [this]() { emit sig_callConnected(); }, Qt::QueuedConnection);
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
        HangUp();
        break;
    default:
        break;
    }
}

void WebRTCMgr::HangUp() {
    RunOnSignalingThread([this]() {
        if (_peer_connection) {
            _peer_connection->Close();
            _peer_connection = nullptr;
        }

        _target_uid = 0;
        _remote_sink = nullptr;
        _remote_video_track = nullptr;
        local_audio_track_ = nullptr;

        QMetaObject::invokeMethod(this, [this]() { emit sig_callEnded(); }, Qt::QueuedConnection);
    });
}

void WebRTCMgr::RejectOrHangUp() {
    QJsonObject p;
    SendSignalingMessage("webrtc_hangup", p);
    HangUp();
}

void WebRTCMgr::HandleRemoteHangUp(int /*fromUid*/) {
    HangUp();
}