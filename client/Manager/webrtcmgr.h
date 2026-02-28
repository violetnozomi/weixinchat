#ifndef WEBRTCMGR_H
#define WEBRTCMGR_H

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <memory>
#include <functional>

// WebRTC
#include "api/scoped_refptr.h"
#include "rtc_base/ref_count.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "rtc_base/thread.h"
#include "api/peer_connection_interface.h"
#include "api/create_peerconnection_factory.h"
#include "api/media_stream_interface.h"
#include "rtc_base/ssl_adapter.h"
#include "p2p/base/port_allocator.h"
#include "pc/video_track_source.h"

class DummySetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
public:
    static webrtc::scoped_refptr<DummySetSessionDescriptionObserver> Create() {
        return webrtc::make_ref_counted<DummySetSessionDescriptionObserver>();
    }

    void OnSuccess() override {
        qDebug() << "[WebRTC] SetSessionDescription success";
    }

    void OnFailure(webrtc::RTCError error) override {
        qDebug() << "[WebRTC] SetSessionDescription failed:" << error.message();
    }
};

class WebRTCMgr : public QObject,
                  public webrtc::PeerConnectionObserver,
                  public webrtc::CreateSessionDescriptionObserver
{
    Q_OBJECT
public:
    // RefCountInterface
    void AddRef() const override {}
    webrtc::RefCountReleaseStatus Release() const override {
        return webrtc::RefCountReleaseStatus::kOtherRefsRemained;
    }

    static WebRTCMgr& GetInstance();

    bool Init();

    // withVideo=false => 纯语音通话
    void StartCall(int targetUid, bool withVideo = true);
    void HangUp();

    // 信令入口（TcpMgr 收包后调用）
    void HandleRemoteOffer(int fromUid, const QString& sdp);
    void HandleRemoteAnswer(int fromUid, const QString& sdp);
    void HandleRemoteIceCandidate(int fromUid,
                                  const QString& sdp_mid,
                                  int sdp_mline_index,
                                  const QString& candidate);
    void HandleRemoteHangUp(int fromUid);

    // 媒体控制
    void startLocalVideo(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink);
    void stopLocalVideo(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink);
    void SetLocalVideoEnabled(bool enabled);
    void setRemoteSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink);

    // UI 行为
    void acceptCall();
    void RejectOrHangUp();

    // CreateSessionDescriptionObserver
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
    void OnFailure(webrtc::RTCError error) override;

    // PeerConnectionObserver
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState) override {}
    void OnRenegotiationNeeded() override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
    void OnAddStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface>) override {}
    void OnRemoveStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface>) override {}

signals:
    // withVideo=false 表示语音来电
    void sig_incomingCall(int from_uid, bool withVideo);
    void sig_callConnected();
    void sig_callEnded();

private:
    WebRTCMgr();
    ~WebRTCMgr();
    WebRTCMgr(const WebRTCMgr&) = delete;
    WebRTCMgr& operator=(const WebRTCMgr&) = delete;

    bool CreatePeerConnection();
    void SendSignalingMessage(const QString& type, const QJsonObject& payload);

    // 线程收敛：所有 PeerConnection / Track 操作走 signaling 线程
    void RunOnSignalingThread(std::function<void()> task);

private:
    int _target_uid = 0;
    bool _with_video = true;

    std::unique_ptr<webrtc::Thread> _network_thread;
    std::unique_ptr<webrtc::Thread> _worker_thread;
    std::unique_ptr<webrtc::Thread> _signaling_thread;

    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> _peer_connection_factory;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> _peer_connection;

    // 本地轨道
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> local_audio_track_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> local_video_track_;

    // 远端视频轨道（后补绑渲染）
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> _remote_video_track;
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* _remote_sink = nullptr;
};

#endif // WEBRTCMGR_H