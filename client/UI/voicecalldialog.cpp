#include "voicecalldialog.h"

#include "webrtcmgr.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVBoxLayout>

VoiceCallDialog::VoiceCallDialog(const QString &peerName, QWidget *parent)
    : QDialog(parent)
    , _tick_timer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("语音通话"));
    setFixedSize(320, 380);
    setObjectName("VoiceCallDialog");
    setStyleSheet(
        "VoiceCallDialog { background: #1a1a2e; }"
        "QLabel#voice_name_lb { color: white; font-size: 18px; font-weight: bold; }"
        "QLabel#voice_status_lb { color: #aaaaaa; font-size: 13px; }"
        "QPushButton#btn_hangup { background: #e74c3c; color: white; border-radius: 30px; "
        "font-size: 14px; font-weight: bold; }"
        "QPushButton#btn_hangup:hover { background: #c0392b; }"
        "QPushButton#btn_hangup:pressed { background: #a93226; }");

    _avatar_lb = new QLabel(this);
    _avatar_lb->setFixedSize(90, 90);
    _avatar_lb->setAlignment(Qt::AlignCenter);
    {
        QPixmap px(90, 90);
        px.fill(Qt::transparent);
        QPainter painter(&px);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor("#444466"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(0, 0, 90, 90);
        painter.setBrush(QColor("#7777aa"));
        painter.drawEllipse(30, 14, 30, 30);
        painter.drawEllipse(10, 56, 70, 50);
        _avatar_lb->setPixmap(px);
    }

    _name_lb = new QLabel(this);
    _name_lb->setObjectName("voice_name_lb");
    _name_lb->setAlignment(Qt::AlignCenter);

    _status_lb = new QLabel(QStringLiteral("呼叫中..."), this);
    _status_lb->setObjectName("voice_status_lb");
    _status_lb->setAlignment(Qt::AlignCenter);

    _btn_hangup_ = new QPushButton(QStringLiteral("挂断"), this);
    _btn_hangup_->setObjectName("btn_hangup");
    _btn_hangup_->setFixedSize(60, 60);
    _btn_hangup_->setCursor(Qt::PointingHandCursor);
    connect(_btn_hangup_, &QPushButton::clicked, this, &VoiceCallDialog::on_hangup_clicked);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(30, 40, 30, 30);
    layout->setSpacing(14);
    layout->addStretch(1);
    layout->addWidget(_avatar_lb, 0, Qt::AlignCenter);
    layout->addWidget(_name_lb, 0, Qt::AlignCenter);
    layout->addWidget(_status_lb, 0, Qt::AlignCenter);
    layout->addStretch(2);

    QHBoxLayout *button_layout = new QHBoxLayout();
    button_layout->addStretch();
    button_layout->addWidget(_btn_hangup_);
    button_layout->addStretch();
    layout->addLayout(button_layout);

    connect(_tick_timer_, &QTimer::timeout, this, &VoiceCallDialog::tick);

    SetPeerName(peerName);
}

VoiceCallDialog::~VoiceCallDialog()
{
    doHangUp();
}

void VoiceCallDialog::SetPeerName(const QString &peerName)
{
    const QString display_name = peerName.isEmpty() ? QStringLiteral("对方") : peerName;
    _name_lb->setText(display_name);
    setWindowTitle(QStringLiteral("与 %1 的语音通话").arg(display_name));
}

void VoiceCallDialog::ApplyCallState(CallSessionMgr::CallState state, bool outgoing)
{
    const bool state_changed = (_call_state_ != state);
    _call_state_ = state;

    switch (state) {
    case CallSessionMgr::CallState::Idle:
        _hung_up_ = true;
        showTerminalState(QStringLiteral("通话已结束"));
        break;
    case CallSessionMgr::CallState::Ringing:
        _tick_timer_->stop();
        _elapsed_sec_ = 0;
        _status_lb->setText(outgoing ? QStringLiteral("呼叫中...") : QStringLiteral("等待接听..."));
        break;
    case CallSessionMgr::CallState::Accepted:
        if (state_changed) {
            _elapsed_sec_ = 0;
        }
        updateConnectedText();
        if (!_tick_timer_->isActive()) {
            _tick_timer_->start(1000);
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

void VoiceCallDialog::closeEvent(QCloseEvent *event)
{
    doHangUp();
    QDialog::closeEvent(event);
}

void VoiceCallDialog::on_hangup_clicked()
{
    doHangUp();
    close();
}

void VoiceCallDialog::doHangUp()
{
    if (_hung_up_) {
        return;
    }
    _hung_up_ = true;
    _tick_timer_->stop();
    WebRTCMgr::GetInstance().RejectOrHangUp();
}

void VoiceCallDialog::tick()
{
    ++_elapsed_sec_;
    updateConnectedText();
}

void VoiceCallDialog::updateConnectedText()
{
    const int minutes = _elapsed_sec_ / 60;
    const int seconds = _elapsed_sec_ % 60;
    _status_lb->setText(
        QStringLiteral("通话中 %1:%2")
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0')));
}

void VoiceCallDialog::showTerminalState(const QString &text)
{
    _tick_timer_->stop();
    _status_lb->setText(text);
    QTimer::singleShot(1800, this, [this]() {
        if (isVisible()) {
            close();
        }
    });
}
