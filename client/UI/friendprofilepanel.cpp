#include "friendprofilepanel.h"
#include "chatbubbleitem.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QFrame>

FriendProfilePanel::FriendProfilePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("FriendProfilePanel");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── 空状态提示 ─────────────────────────────────────────────────────────
    _empty_lb = new QLabel(QString::fromUtf8("从左侧列表选择一位好友"));
    _empty_lb->setObjectName("fpp_empty_lb");
    _empty_lb->setAlignment(Qt::AlignCenter);
    _empty_lb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outer->addWidget(_empty_lb);

    // ── 资料卡主体 ─────────────────────────────────────────────────────────
    _info_widget = new QWidget();
    _info_widget->setObjectName("fpp_info_widget");
    _info_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout *vbox = new QVBoxLayout(_info_widget);
    vbox->setContentsMargins(0, 60, 0, 40);
    vbox->setSpacing(0);
    vbox->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    // 头像
    _avatar_lb = new QLabel();
    _avatar_lb->setObjectName("fpp_avatar");
    _avatar_lb->setFixedSize(80, 80);
    _avatar_lb->setAlignment(Qt::AlignCenter);
    vbox->addWidget(_avatar_lb, 0, Qt::AlignHCenter);
    vbox->addSpacing(16);

    // 昵称
    _name_lb = new QLabel();
    _name_lb->setObjectName("fpp_name_lb");
    _name_lb->setAlignment(Qt::AlignCenter);
    vbox->addWidget(_name_lb, 0, Qt::AlignHCenter);
    vbox->addSpacing(8);

    // uid / 微信号
    _uid_lb = new QLabel();
    _uid_lb->setObjectName("fpp_uid_lb");
    _uid_lb->setAlignment(Qt::AlignCenter);
    vbox->addWidget(_uid_lb, 0, Qt::AlignHCenter);
    vbox->addSpacing(8);

    // 个性签名
    _sig_lb = new QLabel();
    _sig_lb->setObjectName("fpp_sig_lb");
    _sig_lb->setAlignment(Qt::AlignCenter);
    _sig_lb->setWordWrap(true);
    _sig_lb->setMaximumWidth(300);
    vbox->addWidget(_sig_lb, 0, Qt::AlignHCenter);
    vbox->addSpacing(40);

    // 分割线
    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setObjectName("fpp_divider");
    vbox->addWidget(line);
    vbox->addSpacing(40);

    // 发消息按钮
    _msg_btn = new QPushButton(QString::fromUtf8("发消息"));
    _msg_btn->setObjectName("fpp_msg_btn");
    _msg_btn->setFixedSize(120, 40);
    _msg_btn->setCursor(Qt::PointingHandCursor);
    vbox->addWidget(_msg_btn, 0, Qt::AlignHCenter);
    vbox->addSpacing(12);

    // 操作按钮行：删除好友 | 屏蔽
    QHBoxLayout *actRow = new QHBoxLayout;
    actRow->setContentsMargins(0, 0, 0, 0);
    actRow->setSpacing(16);

    _delete_btn = new QPushButton(QString::fromUtf8("删除好友"));
    _delete_btn->setObjectName("fpp_delete_btn");
    _delete_btn->setFixedSize(120, 36);
    _delete_btn->setCursor(Qt::PointingHandCursor);

    _block_btn = new QPushButton(QString::fromUtf8("屏蔽"));
    _block_btn->setObjectName("fpp_block_btn");
    _block_btn->setFixedSize(100, 36);
    _block_btn->setCursor(Qt::PointingHandCursor);

    actRow->addStretch();
    actRow->addWidget(_delete_btn);
    actRow->addWidget(_block_btn);
    actRow->addStretch();
    vbox->addLayout(actRow);

    vbox->addStretch();

    outer->addWidget(_info_widget);

    _info_widget->hide();   // 初始隐藏

    connect(_msg_btn, &QPushButton::clicked, this, [this]() {
        emit sig_send_message(_cur_uid, _cur_name, _cur_avatar);
    });
    connect(_delete_btn, &QPushButton::clicked, this, [this]() {
        if (_cur_uid != -1) emit sig_delete_friend(_cur_uid);
    });
    connect(_block_btn, &QPushButton::clicked, this, [this]() {
        if (_cur_uid != -1) emit sig_block_friend(_cur_uid);
    });
}

void FriendProfilePanel::ShowFriend(int uid, const QString &name,
                                    const QString &avatarPath,
                                    const QString &signature)
{
    _cur_uid    = uid;
    _cur_name   = name;
    _cur_avatar = avatarPath;

    QPixmap px(avatarPath.isEmpty() ? ":/res/head_1.jpg" : avatarPath);
    _avatar_lb->setPixmap(ChatBubbleItem::makeRoundAvatar(px, 80));

    _name_lb->setText(name);
    _uid_lb ->setText(QString::fromUtf8("微信号：wxid_%1").arg(uid));
    _sig_lb ->setText(signature.isEmpty()
                      ? QString::fromUtf8("这个人很懒，什么都没留下")
                      : signature);

    _empty_lb->hide();
    _info_widget->show();
}

void FriendProfilePanel::Reset()
{
    _cur_uid = -1;
    _info_widget->hide();
    _empty_lb->show();
}

void FriendProfilePanel::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    QWidget::paintEvent(event);
}
