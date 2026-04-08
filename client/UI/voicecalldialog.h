#ifndef VOICECALLDIALOG_H
#define VOICECALLDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "CallSessionMgr.h"

/**
 * VoiceCallDialog – 纯语音通话界面（无视频面板）
 *
 * 状态流程：Calling → Connected / Ended
 * - 呼叫中：显示旋转等待动画 + "呼叫中..."
 * - 已连接：显示通话计时（00:00、00:01...）
 * - 结束后：自动关闭（2 秒）或手动挂断
 */
class VoiceCallDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VoiceCallDialog(const QString &peerName, QWidget *parent = nullptr);
    ~VoiceCallDialog() override;

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

    QLabel      *_avatar_lb      = nullptr;
    QLabel      *_name_lb        = nullptr;
    QLabel      *_status_lb      = nullptr;
    QPushButton *_btn_hangup_    = nullptr;

    QTimer      *_tick_timer_    = nullptr;
    int          _elapsed_sec_   = 0;

    CallSessionMgr::CallState _call_state_ = CallSessionMgr::CallState::Idle;
    bool         _hung_up_       = false;
};

#endif // VOICECALLDIALOG_H
