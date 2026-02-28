#include "videocalldialog.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include "webrtcmgr.h"

VideoCallDialog::VideoCallDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("通话中..."));
    resize(850, 600);

    local_render_ = new VideoRender(this);
    remote_render_ = new VideoRender(this);
    local_render_->setFixedSize(240, 180);

    btn_hangup_ = new QPushButton(QString::fromUtf8("挂断"), this);
    btn_hangup_->setStyleSheet(
        "QPushButton { background-color: #ff4d4f; color: white; border-radius: 5px; "
        "padding: 10px; font-weight: bold; font-size: 14px; }");
    btn_hangup_->setCursor(Qt::PointingHandCursor);
    connect(btn_hangup_, &QPushButton::clicked, this, &VideoCallDialog::on_hangup_clicked);

    QVBoxLayout* leftLayout = new QVBoxLayout();
    leftLayout->addWidget(local_render_);
    leftLayout->addStretch();
    leftLayout->addWidget(btn_hangup_);

    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addWidget(remote_render_, 3);

    setLayout(mainLayout);
}

VideoCallDialog::~VideoCallDialog()
{
    // 不在析构里主动 HangUp，避免重复状态变更
    if (local_render_) {
        WebRTCMgr::GetInstance().stopLocalVideo(local_render_->GetVideoSink());
    }
}

webrtc::VideoSinkInterface<webrtc::VideoFrame>* VideoCallDialog::GetLocalVideoSink() {
    return local_render_ ? local_render_->GetVideoSink() : nullptr;
}

webrtc::VideoSinkInterface<webrtc::VideoFrame>* VideoCallDialog::GetRemoteVideoSink() {
    return remote_render_ ? remote_render_->GetVideoSink() : nullptr;
}

void VideoCallDialog::on_hangup_clicked()
{
    WebRTCMgr::GetInstance().RejectOrHangUp(); // 关键：通知对端
    close();
}