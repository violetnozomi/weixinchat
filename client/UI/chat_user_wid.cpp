#include "chat_user_wid.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDebug>
#include <QPainterPath>
#include <QFile>

ChatUserWid::ChatUserWid(QWidget *parent)
    : QWidget(parent)
    , _uid(0)
    , _selected(false)
    , _hovered(false)
{
    setupUI();
    setFixedHeight(60);  // 仿微信好友列表项高度
    setMouseTracking(true);
}

ChatUserWid::~ChatUserWid()
{
}

void ChatUserWid::setupUI()
{
    // 主布局
    QHBoxLayout* main_layout = new QHBoxLayout(this);
    main_layout->setContentsMargins(10, 5, 10, 5);
    main_layout->setSpacing(10);
    
    // 头像（圆角矩形或圆形）
    _avatar_label = new QLabel(this);
    _avatar_label->setFixedSize(45, 45);
    _avatar_label->setStyleSheet(
        "QLabel { "
        "   background-color: #07C160; "
        "   color: white; "
        "   font-size: 18px; "
        "   border-radius: 4px; "
        "}"
    );
    _avatar_label->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(_avatar_label);
    
    // 右侧信息区域
    QVBoxLayout* info_layout = new QVBoxLayout();
    info_layout->setSpacing(4);
    info_layout->setContentsMargins(0, 0, 0, 0);
    
    // 第一行：用户名 + 时间
    QHBoxLayout* top_layout = new QHBoxLayout();
    top_layout->setSpacing(5);
    
    _name_label = new QLabel(this);
    _name_label->setStyleSheet(
        "QLabel { "
        "   font-size: 14px; "
        "   color: #000000; "
        "   font-weight: bold; "
        "}"
    );
    top_layout->addWidget(_name_label);
    top_layout->addStretch();
    
    _time_label = new QLabel(this);
    _time_label->setStyleSheet(
        "QLabel { "
        "   font-size: 11px; "
        "   color: #999999; "
        "}"
    );
    top_layout->addWidget(_time_label);
    
    info_layout->addLayout(top_layout);
    
    // 第二行：最后消息 + 未读数
    QHBoxLayout* bottom_layout = new QHBoxLayout();
    bottom_layout->setSpacing(5);
    
    _last_msg_label = new QLabel(this);
    _last_msg_label->setStyleSheet(
        "QLabel { "
        "   font-size: 12px; "
        "   color: #666666; "
        "}"
    );
    _last_msg_label->setWordWrap(false);
    bottom_layout->addWidget(_last_msg_label, 1);
    
    _unread_label = new QLabel(this);
    _unread_label->setFixedSize(18, 18);
    _unread_label->setStyleSheet(
        "QLabel { "
        "   background-color: #FA5151; "
        "   color: white; "
        "   font-size: 10px; "
        "   border-radius: 9px; "
        "}"
    );
    _unread_label->setAlignment(Qt::AlignCenter);
    _unread_label->hide();  // 默认隐藏
    bottom_layout->addWidget(_unread_label);
    
    info_layout->addLayout(bottom_layout);
    
    main_layout->addLayout(info_layout, 1);
}

void ChatUserWid::SetInfo(int uid, const QString& name, const QString& avatar)
{
    _uid = uid;
    _name = name;
    _avatar_path = avatar;
    
    _name_label->setText(name);
    
    // 如果有头像路径，加载头像；否则显示首字母或默认头像
    if (!avatar.isEmpty()) {
        QPixmap pixmap(avatar);
        if (!pixmap.isNull()) {
            // 缩放头像并裁剪为正方形
            QPixmap scaled = pixmap.scaled(_avatar_label->size(), 
                                          Qt::KeepAspectRatioByExpanding, 
                                          Qt::SmoothTransformation);
            
            // 居中裁剪
            int x = (scaled.width() - _avatar_label->width()) / 2;
            int y = (scaled.height() - _avatar_label->height()) / 2;
            QPixmap cropped = scaled.copy(x, y, _avatar_label->width(), _avatar_label->height());
            
            // 创建圆角遮罩
            QPixmap rounded(cropped.size());
            rounded.fill(Qt::transparent);
            
            QPainter painter(&rounded);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            
            // 绘制圆角矩形路径
            QPainterPath path;
            path.addRoundedRect(0, 0, cropped.width(), cropped.height(), 4, 4);
            painter.setClipPath(path);
            painter.drawPixmap(0, 0, cropped);
            
            _avatar_label->setPixmap(rounded);
            _avatar_label->setText("");
            _avatar_label->setStyleSheet(
                "QLabel { "
                "   background-color: transparent; "
                "   border: none; "
                "}"
            );
        } else {
            // 图片加载失败，显示首字母
            QString first_char = name.isEmpty() ? "?" : name.left(1);
            _avatar_label->setText(first_char);
            _avatar_label->setStyleSheet(
                "QLabel { "
                "   background-color: #07C160; "
                "   color: white; "
                "   font-size: 18px; "
                "   border-radius: 4px; "
                "}"
            );
        }
    } else {
        // 显示用户名首字母
        QString first_char = name.isEmpty() ? "?" : name.left(1);
        _avatar_label->setText(first_char);
        _avatar_label->setStyleSheet(
            "QLabel { "
            "   background-color: #07C160; "
            "   color: white; "
            "   font-size: 18px; "
            "   border-radius: 4px; "
            "}"
        );
    }
}

void ChatUserWid::SetLastMsg(const QString& msg, const QString& time)
{
    // 截断消息，最多显示20个字符
    QString display_msg = msg;
    if (msg.length() > 20) {
        display_msg = msg.left(20) + "...";
    }
    _last_msg_label->setText(display_msg);
    _time_label->setText(time);
}

void ChatUserWid::SetUnreadCount(int count)
{
    if (count > 0) {
        if (count > 99) {
            _unread_label->setText("99+");
        } else {
            _unread_label->setText(QString::number(count));
        }
        _unread_label->show();
    } else {
        _unread_label->hide();
    }
}

void ChatUserWid::SetSelected(bool selected)
{
    _selected = selected;
    update();  // 触发重绘
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
        emit sig_clicked(_uid);
    }
    QWidget::mousePressEvent(event);
}

void ChatUserWid::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 根据状态绘制背景
    if (_selected) {
        // 选中状态：浅灰色背景
        painter.fillRect(rect(), QColor(230, 230, 230));
    } else if (_hovered) {
        // 悬停状态：更浅的灰色
        painter.fillRect(rect(), QColor(245, 245, 245));
    } else {
        // 默认状态：白色背景
        painter.fillRect(rect(), QColor(255, 255, 255));
    }
    
    QWidget::paintEvent(event);
}
