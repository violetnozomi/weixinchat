#ifndef VIDEOCALLDIALOG_H
#define VIDEOCALLDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "CallSessionMgr.h"
#include "videorender.h"
#include "api/video/video_sink_interface.h"

class VideoCallDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VideoCallDialog(const QString &peerName = QString(), QWidget *parent = nullptr);
    ~VideoCallDialog();

    webrtc::VideoSinkInterface<webrtc::VideoFrame>* GetRemoteVideoSink();
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* GetLocalVideoSink();
    void SetPeerName(const QString &peerName);
    void ApplyCallState(CallSessionMgr::CallState state, bool outgoing);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_hangup_clicked();

private:
    void doHangUp();  // safe to call multiple times
    void tick();
    void updateConnectedText();
    void showTerminalState(const QString &text);

    VideoRender* local_render_  = nullptr;
    VideoRender* remote_render_ = nullptr;
    QLabel*      name_lb_       = nullptr;
    QLabel*      status_lb_     = nullptr;
    QPushButton* btn_hangup_    = nullptr;
    QTimer*      tick_timer_    = nullptr;
    int          elapsed_sec_   = 0;
    CallSessionMgr::CallState call_state_ = CallSessionMgr::CallState::Idle;
    bool         _hung_up_      = false;
};

#endif // VIDEOCALLDIALOG_H
