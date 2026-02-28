#include "addfriendwidget.h"
#include "chatbubbleitem.h"   // makeRoundAvatar

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

AddFriendWidget::AddFriendWidget(int uid,
                                 const QString &name,
                                 const QString &avatarPath,
                                 QWidget *parent)
    : QDialog(parent), _uid(uid), _name(name)
{
    setWindowTitle("添加好友");
    setFixedSize(380, 310);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setAttribute(Qt::WA_DeleteOnClose);

    // ── 全局背景 ──────────────────────────────────────────────
    setStyleSheet(
        "AddFriendWidget {"
        "   background: #f5f5f5;"
        "}"
        // 头部灰区
        "#afd_header {"
        "   background: #ededed;"
        "}"
        // 姓名
        "#afd_name_lb {"
        "   font-size: 17px;"
        "   font-weight: bold;"
        "   color: #1a1a1a;"
        "   font-family: 'Microsoft YaHei';"
        "}"
        // 提示文字
        "#afd_hint_lb {"
        "   font-size: 12px;"
        "   color: #999;"
        "   font-family: 'Microsoft YaHei';"
        "}"
        // 输入框
        "#afd_msg_edit {"
        "   background: white;"
        "   border: 1px solid #e0e0e0;"
        "   border-radius: 4px;"
        "   font-size: 14px;"
        "   padding: 6px;"
        "   font-family: 'Microsoft YaHei';"
        "}"
        // 发送按钮（微信绿）
        "#afd_send_btn {"
        "   background: #07c160;"
        "   color: white;"
        "   border: none;"
        "   border-radius: 4px;"
        "   font-size: 14px;"
        "   font-family: 'Microsoft YaHei';"
        "}"
        "#afd_send_btn:hover  { background: #06ad56; }"
        "#afd_send_btn:pressed{ background: #059a4d; }"
        // 取消按钮
        "#afd_cancel_btn {"
        "   background: #e9e9e9;"
        "   color: #333;"
        "   border: 1px solid #d0d0d0;"
        "   border-radius: 4px;"
        "   font-size: 14px;"
        "   font-family: 'Microsoft YaHei';"
        "}"
        "#afd_cancel_btn:hover  { background: #ddd; }"
        "#afd_cancel_btn:pressed{ background: #ccc; }"
    );

    // ── 根布局 ────────────────────────────────────────────────
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── 头部：头像 + 姓名 ─────────────────────────────────────
    QWidget *header = new QWidget();
    header->setObjectName("afd_header");
    header->setFixedHeight(90);

    QHBoxLayout *hdrLayout = new QHBoxLayout(header);
    hdrLayout->setContentsMargins(24, 0, 24, 0);
    hdrLayout->setSpacing(16);

    // 圆形头像
    _avatar_lb = new QLabel();
    _avatar_lb->setFixedSize(56, 56);
    _avatar_lb->setScaledContents(false);
    QPixmap src(avatarPath);
    if (src.isNull()) src.load(":/res/head_1.jpg");
    _avatar_lb->setPixmap(ChatBubbleItem::makeRoundAvatar(src, 56));

    // 姓名
    _name_lb = new QLabel(name);
    _name_lb->setObjectName("afd_name_lb");

    hdrLayout->addWidget(_avatar_lb);
    hdrLayout->addWidget(_name_lb);
    hdrLayout->addStretch();

    root->addWidget(header);

    // ── 分隔线 ────────────────────────────────────────────────
    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setStyleSheet("color: #d9d9d9;");
    root->addWidget(line);

    // ── 正文区 ────────────────────────────────────────────────
    QWidget *body = new QWidget();
    QVBoxLayout *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(24, 14, 24, 14);
    bodyLayout->setSpacing(8);

    // "发送添加好友申请"
    _hint_lb = new QLabel("发送添加好友申请");
    _hint_lb->setObjectName("afd_hint_lb");

    // 验证消息输入框
    _msg_edit = new QTextEdit();
    _msg_edit->setObjectName("afd_msg_edit");
    _msg_edit->setFixedHeight(90);
    _msg_edit->setPlaceholderText(QString("我是%1，请加我为好友。").arg(name));
    _msg_edit->setPlainText(QString("我是%1，请加我为好友。").arg(name));
    // 限制最多 100 字
    connect(_msg_edit, &QTextEdit::textChanged, this, [this]() {
        QString txt = _msg_edit->toPlainText();
        if (txt.length() > 100) {
            QTextCursor cur = _msg_edit->textCursor();
            _msg_edit->setPlainText(txt.left(100));
            _msg_edit->setTextCursor(cur);
        }
    });

    bodyLayout->addWidget(_hint_lb);
    bodyLayout->addWidget(_msg_edit);

    root->addWidget(body);
    root->addStretch();

    // ── 分隔线 ────────────────────────────────────────────────
    QFrame *line2 = new QFrame();
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Plain);
    line2->setStyleSheet("color: #d9d9d9;");
    root->addWidget(line2);

    // ── 底部按钮栏 ────────────────────────────────────────────
    QWidget *footer = new QWidget();
    QHBoxLayout *btnLayout = new QHBoxLayout(footer);
    btnLayout->setContentsMargins(24, 12, 24, 12);
    btnLayout->setSpacing(12);

    _cancel_btn = new QPushButton("取消");
    _cancel_btn->setObjectName("afd_cancel_btn");
    _cancel_btn->setFixedSize(100, 36);

    _send_btn = new QPushButton("发送");
    _send_btn->setObjectName("afd_send_btn");
    _send_btn->setFixedSize(100, 36);

    btnLayout->addStretch();
    btnLayout->addWidget(_cancel_btn);
    btnLayout->addWidget(_send_btn);

    root->addWidget(footer);

    // ── 信号连接 ──────────────────────────────────────────────
    connect(_cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    connect(_send_btn,   &QPushButton::clicked, this, [this]() {
        QString msg = _msg_edit->toPlainText().trimmed();
        emit sig_send_request(_uid, msg);
        accept();
    });
}
