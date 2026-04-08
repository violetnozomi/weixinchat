#include "chat_user_wid.h"

#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

ChatUserWid::ChatUserWid(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setFixedHeight(60);
    setMouseTracking(true);
}

ChatUserWid::~ChatUserWid() = default;

void ChatUserWid::setupUI()
{
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 5, 10, 5);
    mainLayout->setSpacing(10);

    _avatar_label = new QLabel(this);
    _avatar_label->setFixedSize(45, 45);
    _avatar_label->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(_avatar_label);

    auto *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    auto *topLayout = new QHBoxLayout();
    topLayout->setSpacing(5);

    _name_label = new QLabel(this);
    _name_label->setStyleSheet(
        "QLabel { font-size: 14px; color: #000000; font-weight: bold; }");
    topLayout->addWidget(_name_label);
    topLayout->addStretch();

    _time_label = new QLabel(this);
    _time_label->setStyleSheet("QLabel { font-size: 11px; color: #999999; }");
    topLayout->addWidget(_time_label);

    infoLayout->addLayout(topLayout);

    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(5);

    _last_msg_label = new QLabel(this);
    _last_msg_label->setStyleSheet("QLabel { font-size: 12px; color: #666666; }");
    _last_msg_label->setWordWrap(false);
    bottomLayout->addWidget(_last_msg_label, 1);

    _unread_label = new QLabel(this);
    _unread_label->setFixedSize(18, 18);
    _unread_label->setStyleSheet(
        "QLabel { background-color: #FA5151; color: white; font-size: 10px; border-radius: 9px; }");
    _unread_label->setAlignment(Qt::AlignCenter);
    _unread_label->hide();
    bottomLayout->addWidget(_unread_label);

    infoLayout->addLayout(bottomLayout);
    mainLayout->addLayout(infoLayout, 1);
}

void ChatUserWid::SetInfo(const QString &conversationKey, const QString &name, const QString &avatar)
{
    _conversation_key = conversationKey;
    _name = name;
    _avatar_path = avatar;
    _name_label->setText(name);

    QPixmap pixmap(avatar);
    if (!avatar.isEmpty() && !pixmap.isNull()) {
        const QPixmap scaled = pixmap.scaled(
            _avatar_label->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = (scaled.width() - _avatar_label->width()) / 2;
        const int y = (scaled.height() - _avatar_label->height()) / 2;
        const QPixmap cropped = scaled.copy(x, y, _avatar_label->width(), _avatar_label->height());

        QPixmap rounded(cropped.size());
        rounded.fill(Qt::transparent);

        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath path;
        path.addRoundedRect(0, 0, cropped.width(), cropped.height(), 4, 4);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, cropped);

        _avatar_label->setPixmap(rounded);
        _avatar_label->setText(QString());
        _avatar_label->setStyleSheet("QLabel { background-color: transparent; border: none; }");
        return;
    }

    const QString firstChar = name.isEmpty() ? "?" : name.left(1);
    _avatar_label->setText(firstChar);
    _avatar_label->setStyleSheet(
        "QLabel { background-color: #07C160; color: white; font-size: 18px; border-radius: 4px; }");
}

void ChatUserWid::SetLastMsg(const QString &msg, const QString &time)
{
    _last_msg = msg;
    _time = time;
    QString displayMsg = msg;
    if (displayMsg.length() > 20) {
        displayMsg = displayMsg.left(20) + "...";
    }
    _last_msg_label->setText(displayMsg);
    _time_label->setText(time);
}

void ChatUserWid::SetUnreadCount(int count)
{
    _unread_count = count;
    if (count > 0) {
        _unread_label->setText(count > 99 ? "99+" : QString::number(count));
        _unread_label->show();
        return;
    }
    _unread_label->hide();
}

void ChatUserWid::SetSelected(bool selected)
{
    _selected = selected;
    update();
}

void ChatUserWid::enterEvent(QEnterEvent *event)
{
    _hovered = true;
    update();
    QWidget::enterEvent(event);
}

void ChatUserWid::leaveEvent(QEvent *event)
{
    _hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void ChatUserWid::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit sig_clicked(_conversation_key);
    }
    QWidget::mousePressEvent(event);
}

void ChatUserWid::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (_selected) {
        painter.fillRect(rect(), QColor(230, 230, 230));
    } else if (_hovered) {
        painter.fillRect(rect(), QColor(245, 245, 245));
    } else {
        painter.fillRect(rect(), QColor(255, 255, 255));
    }

    QWidget::paintEvent(event);
}
