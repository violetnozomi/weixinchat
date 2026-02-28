#ifndef FRIENDREQUESTITEM_H
#define FRIENDREQUESTITEM_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QDateTime>
#include "FriendRequest.h"

// ── 单条好友申请 UI 行 ────────────────────────────────────────────────────────
class FriendRequestItem : public QWidget
{
    Q_OBJECT
public:
    explicit FriendRequestItem(const FriendRequest &req, QWidget *parent = nullptr);

    int  GetUid() const { return _uid; }
    void MarkAccepted();
    void MarkRejected();

signals:
    void sig_accepted(int uid);
    void sig_rejected(int uid);

protected:
    void enterEvent(QEnterEvent *event)   override;
    void leaveEvent(QEvent     *event)   override;
    void paintEvent(QPaintEvent *event)  override;

private:
    int          _uid;
    bool         _hovered   = false;
    bool         _finalised = false;   // true once accepted or rejected

    QLabel       *_avatar_lb;
    QLabel       *_name_lb;
    QLabel       *_msg_lb;
    QLabel       *_time_lb;
    QPushButton  *_accept_btn;
    QPushButton  *_reject_btn;
    QLabel       *_status_lb;          // "已添加" / "已拒绝"
};

#endif // FRIENDREQUESTITEM_H
