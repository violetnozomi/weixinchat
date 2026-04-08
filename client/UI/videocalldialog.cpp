#include "videocalldialog.h"

#include "webrtcmgr.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>

VideoCallDialog::VideoCallDialog(const QString &peerName, QWidget *parent)
    : QDialog(parent)
    , tick_timer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("视频通话"));
    resize(850, 600);
    setObjectName("VideoCallDialog");
    setStyleSheet(
        "VideoCallDialog { background: #111111; }"
        "QLabel#video_call_name { color: white; font-size: 18px; font-weight: bold; }"
        "QLabel#video_call_status { color: #c8c8c8; font-size: 13px; }");

    name_lb_ = new QLabel(this);
    name_lb_->setObjectName("video_call_name");

    status_lb_ = new QLabel(QStringLiteral("呼叫中..."), this);
    status_lb_->setObjectName("video_call_status");

    local_render_ = new VideoRender(this);
    local_render_->setFixedSize(240, 180);

    remote_render_ = new VideoRender(this);

    btn_hangup_ = new QPushButton(QStringLiteral("挂断"), this);
    btn_hangup_->setStyleSheet(
        "QPushButton { background-color: #ff4d4f; color: white; border-radius: 5px; "
        "padding: 10px; font-weight: bold; font-size: 14px; }");
    btn_hangup_->setCursor(Qt::PointingHandCursor);
    connect(btn_hangup_, &QPushButton::clicked, this, &VideoCallDialog::on_hangup_clicked);

    QVBoxLayout *left_layout = new QVBoxLayout();
    left_layout->setSpacing(12);
    left_layout->addWidget(name_lb_);
    left_layout->addWidget(status_lb_);
    left_layout->addWidget(local_render_);
    left_layout->addStretch();
    left_layout->addWidget(btn_hangup_);

    QHBoxLayout *main_layout = new QHBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(20);
    main_layout->addLayout(left_layout, 1);
    main_layout->addWidget(remote_render_, 3);

    connect(tick_timer_, &QTimer::timeout, this, &VideoCallDialog::tick);

    SetPeerName(peerName);
}

VideoCallDialog::~VideoCallDialog()
{
    if (local_render_) {
        local_render_->Stop();
    }
    if (remote_render_) {
        remote_render_->Stop();
    }
    doHangUp();
    if (local_render_) {
        WebRTCMgr::GetInstance().stopLocalVideo(local_render_->GetVideoSink());
    }
    if (remote_render_) {
        WebRTCMgr::GetInstance().setRemoteSink(nullptr);
    }
}

void VideoCallDialog::SetPeerName(const QString &peerName)
{
    const QString display_name = peerName.isEmpty() ? QStringLiteral("对方") : peerName;
    name_lb_->setText(display_name);
    setWindowTitle(QStringLiteral("与 %1 的视频通话").arg(display_name));
}

void VideoCallDialog::ApplyCallState(CallSessionMgr::CallState state, bool outgoing)
{
    const bool state_changed = (call_state_ != state);
    call_state_ = state;

    switch (state) {
    case CallSessionMgr::CallState::Idle:
        _hung_up_ = true;
        showTerminalState(QStringLiteral("通话已结束"));
        break;
    case CallSessionMgr::CallState::Ringing:
        tick_timer_->stop();
        elapsed_sec_ = 0;
        status_lb_->setText(outgoing ? QStringLiteral("正在呼叫...") : QStringLiteral("等待接听..."));
        break;
    case CallSessionMgr::CallState::Accepted:
        if (state_changed) {
            elapsed_sec_ = 0;
        }
        updateConnectedText();
        if (!tick_timer_->isActive()) {
            tick_timer_->start(1000);
        }
        break;
    case CallSessionMgr::CallState::Rejected:
        _hung_up_ = true;
        showTerminalState(outgoing ? QStringLiteral("对方已拒绝") : QStringLiteral("已拒绝通话"));
        break;
    case CallSessionMgr::CallState::Ended:
        _hung_up_ = true;
        showTerminalState(QStringLiteral("通话已结束"));
        break;
    case CallSessionMgr::CallState::Timeout:
        _hung_up_ = true;
        showTerminalState(outgoing ? QStringLiteral("无人接听") : QStringLiteral("通话超时"));
        break;
    }
}

void VideoCallDialog::closeEvent(QCloseEvent *event)
{
    doHangUp();
    QDialog::closeEvent(event);
}

webrtc::VideoSinkInterface<webrtc::VideoFrame>* VideoCallDialog::GetLocalVideoSink()
{
    return local_render_ ? local_render_->GetVideoSink() : nullptr;
}

webrtc::VideoSinkInterface<webrtc::VideoFrame>* VideoCallDialog::GetRemoteVideoSink()
{
    return remote_render_ ? remote_render_->GetVideoSink() : nullptr;
}

void VideoCallDialog::on_hangup_clicked()
{
    doHangUp();
    close();
}

void VideoCallDialog::doHangUp()
{
    if (_hung_up_) {
        return;
    }
    _hung_up_ = true;
    tick_timer_->stop();
    WebRTCMgr::GetInstance().RejectOrHangUp();
}

void VideoCallDialog::tick()
{
    ++elapsed_sec_;
    updateConnectedText();
}

void VideoCallDialog::updateConnectedText()
{
    const int minutes = elapsed_sec_ / 60;
    const int seconds = elapsed_sec_ % 60;
    status_lb_->setText(
        QStringLiteral("视频通话中 %1:%2")
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0')));
}

void VideoCallDialog::showTerminalState(const QString &text)
{
    tick_timer_->stop();
    status_lb_->setText(text);
    QTimer::singleShot(1800, this, [this]() {
        if (isVisible()) {
            close();
        }
    });
}
