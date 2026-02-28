#include "friendrequestitem.h"
#include "chatbubbleitem.h"   // makeRoundAvatar
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QDateTime>
#include <QStyle>

FriendRequestItem::FriendRequestItem(const FriendRequest &req, QWidget *parent)
    : QWidget(parent)
    , _uid(req.uid)
{
    setObjectName("FriendRequestItem");
    setFixedHeight(76);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_StyledBackground, true);

    // ── 头像 ──────────────────────────────────────────────────────────────
    _avatar_lb = new QLabel(this);
    _avatar_lb->setFixedSize(46, 46);
    _avatar_lb->setObjectName("fri_req_avatar");
    QPixmap src(req.avatarPath.isEmpty() ? ":/res/head_1.jpg" : req.avatarPath);
    _avatar_lb->setPixmap(ChatBubbleItem::makeRoundAvatar(src, 46));
    _avatar_lb->setAlignment(Qt::AlignCenter);

    // ── 文字区 ────────────────────────────────────────────────────────────
    _name_lb = new QLabel(req.name.isEmpty() ? QString("uid:%1").arg(req.uid) : req.name);
    _name_lb->setObjectName("fri_req_name");

    QString msgText = req.message.isEmpty()
                      ? QString::fromUtf8("[暂无留言]")
                      : req.message;
    if (msgText.length() > 40) msgText = msgText.left(38) + "...";
    _msg_lb = new QLabel(msgText);
    _msg_lb->setObjectName("fri_req_msg");

    QVBoxLayout *textVBox = new QVBoxLayout();
    textVBox->setContentsMargins(0, 0, 0, 0);
    textVBox->setSpacing(4);
    textVBox->addWidget(_name_lb);
    textVBox->addWidget(_msg_lb);

    // ── 时间 + 操作区（右侧竖排）──────────────────────────────────────────
    _time_lb = new QLabel(req.time.isValid()
                          ? req.time.toString("MM-dd hh:mm")
                          : QDateTime::currentDateTime().toString("MM-dd hh:mm"));
    _time_lb->setObjectName("fri_req_time");
    _time_lb->setAlignment(Qt::AlignRight | Qt::AlignTop);
    _time_lb->setFixedWidth(80);

    _accept_btn = new QPushButton(QString::fromUtf8("接受"));
    _accept_btn->setObjectName("fri_accept_btn");
    _accept_btn->setFixedSize(60, 26);
    _accept_btn->setCursor(Qt::PointingHandCursor);

    _reject_btn = new QPushButton(QString::fromUtf8("拒绝"));
    _reject_btn->setObjectName("fri_reject_btn");
    _reject_btn->setFixedSize(60, 26);
    _reject_btn->setCursor(Qt::PointingHandCursor);

    _status_lb = new QLabel();
    _status_lb->setObjectName("fri_status_lb");
    _status_lb->setAlignment(Qt::AlignCenter);
    _status_lb->setFixedWidth(70);
    _status_lb->hide();

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(6);
    btnRow->addWidget(_accept_btn);
    btnRow->addWidget(_reject_btn);

    QVBoxLayout *rightVBox = new QVBoxLayout();
    rightVBox->setContentsMargins(0, 0, 0, 0);
    rightVBox->setSpacing(4);
    rightVBox->addWidget(_time_lb);
    rightVBox->addLayout(btnRow);
    rightVBox->addWidget(_status_lb, 0, Qt::AlignCenter);

    // ── 总行布局 ───────────────────────────────────────────────────────────
    QHBoxLayout *row = new QHBoxLayout(this);
    row->setContentsMargins(12, 0, 12, 0);
    row->setSpacing(10);
    row->addWidget(_avatar_lb);
    row->addLayout(textVBox, 1);
    row->addLayout(rightVBox);

    // ── 信号连接 ───────────────────────────────────────────────────────────
    connect(_accept_btn, &QPushButton::clicked, this, [this]() {
        if (_finalised) return;
        MarkAccepted();
        emit sig_accepted(_uid);
    });
    connect(_reject_btn, &QPushButton::clicked, this, [this]() {
        if (_finalised) return;
        MarkRejected();
        emit sig_rejected(_uid);
    });
}

void FriendRequestItem::MarkAccepted()
{
    _finalised = true;
    _accept_btn->hide();
    _reject_btn->hide();
    _status_lb->setText(QString::fromUtf8("已添加"));
    _status_lb->setProperty("status", "accepted");
    _status_lb->style()->unpolish(_status_lb);
    _status_lb->style()->polish(_status_lb);
    _status_lb->show();
}

void FriendRequestItem::MarkRejected()
{
    _finalised = true;
    _accept_btn->hide();
    _reject_btn->hide();
    _status_lb->setText(QString::fromUtf8("已拒绝"));
    _status_lb->setProperty("status", "rejected");
    _status_lb->style()->unpolish(_status_lb);
    _status_lb->style()->polish(_status_lb);
    _status_lb->show();
}

void FriendRequestItem::enterEvent(QEnterEvent *event)
{
    _hovered = true;
    update();
    QWidget::enterEvent(event);
}

void FriendRequestItem::leaveEvent(QEvent *event)
{
    _hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void FriendRequestItem::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (_hovered) {
        p.fillRect(rect(), QColor(0xF0, 0xF0, 0xF0));
    } else {
        p.fillRect(rect(), QColor(0xF9, 0xF9, 0xF9));
    }
    // 底部分割线
    p.setPen(QColor(0xE5, 0xE5, 0xE5));
    p.drawLine(12, height() - 1, width() - 12, height() - 1);
    QWidget::paintEvent(event);
}
