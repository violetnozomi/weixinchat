#include "searchuseritem.h"
#include "chatbubbleitem.h"   // makeRoundAvatar

#include <QPainter>
#include <QMouseEvent>

SearchUserItem::SearchUserItem(int uid,
                               const QString &name,
                               const QString &wechatId,
                               const QString &avatarPath,
                               State state,
                               QWidget *parent)
    : QWidget(parent), _uid(uid), _name(name), _avatarPath(avatarPath)
{
    setFixedHeight(70);
    setMouseTracking(true);

    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 0, 16, 0);
    root->setSpacing(12);

    // 头像
    _avatar_lb = new QLabel(this);
    _avatar_lb->setFixedSize(46, 46);
    _avatar_lb->setAlignment(Qt::AlignCenter);
    QPixmap src(avatarPath.isEmpty() ? ":/res/head_1.jpg" : avatarPath);
    _avatar_lb->setPixmap(ChatBubbleItem::makeRoundAvatar(src, 46));

    // 中间信息列
    QVBoxLayout *infoCol = new QVBoxLayout;
    infoCol->setSpacing(3);
    infoCol->setContentsMargins(0, 0, 0, 0);

    _name_lb = new QLabel(name, this);
    _name_lb->setObjectName("sui_name_lb");

    _id_lb = new QLabel(QString("微信号：%1").arg(wechatId), this);
    _id_lb->setObjectName("sui_id_lb");

    infoCol->addWidget(_name_lb);
    infoCol->addWidget(_id_lb);

    // 右侧按钮
    _add_btn = new QPushButton(this);
    _add_btn->setFixedSize(72, 32);
    _add_btn->setCursor(Qt::PointingHandCursor);

    if (state == State::AlreadyFriend) {
        _add_btn->setText("已添加");
        _add_btn->setObjectName("sui_added_btn");
        _add_btn->setEnabled(false);
    } else {
        _add_btn->setText("添加");
        _add_btn->setObjectName("sui_add_btn");
        connect(_add_btn, &QPushButton::clicked, this, [this]() {
            emit sig_add_clicked(_uid, _name, _avatarPath);
        });
    }

    root->addWidget(_avatar_lb, 0, Qt::AlignVCenter);
    root->addLayout(infoCol, 1);
    root->addWidget(_add_btn, 0, Qt::AlignVCenter);
}

void SearchUserItem::enterEvent(QEnterEvent *event)
{
    _hovered = true;
    update();
    QWidget::enterEvent(event);
}

void SearchUserItem::leaveEvent(QEvent *event)
{
    _hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void SearchUserItem::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(_hovered ? QColor(0xEC, 0xEC, 0xEC) : QColor(0xF7, 0xF7, 0xF7));
    p.drawRect(rect());

    // 底部分割线
    p.setPen(QColor(0xE5, 0xE5, 0xE5));
    p.drawLine(16, height() - 1, width() - 16, height() - 1);
}
