#include "friendverifypanel.h"
#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

FriendVerifyPanel::FriendVerifyPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("FriendVerifyPanel");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout *outerVBox = new QVBoxLayout(this);
    outerVBox->setContentsMargins(0, 0, 0, 0);
    outerVBox->setSpacing(0);

    // ── 标题栏 ────────────────────────────────────────────────────────────
    QWidget *titleBar = new QWidget();
    titleBar->setObjectName("fvp_title_bar");
    titleBar->setFixedHeight(48);
    titleBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QHBoxLayout *titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(16, 0, 16, 0);

    QLabel *titleLb = new QLabel(QString::fromUtf8("新的朋友"));
    titleLb->setObjectName("fvp_title_lb");
    titleRow->addWidget(titleLb);
    titleRow->addStretch();

    outerVBox->addWidget(titleBar);

    // ── 分割线 ────────────────────────────────────────────────────────────
    QFrame *divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setObjectName("fvp_divider");
    outerVBox->addWidget(divider);

    // ── 滚动区 ────────────────────────────────────────────────────────────
    _scroll = new QScrollArea();
    _scroll->setWidgetResizable(true);
    _scroll->setFrameShape(QFrame::NoFrame);
    _scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    _content = new QWidget();
    _content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    _layout = new QVBoxLayout(_content);
    _layout->setContentsMargins(0, 0, 0, 0);
    _layout->setSpacing(0);
    _layout->addStretch();     // 撑开底部空白

    _scroll->setWidget(_content);
    outerVBox->addWidget(_scroll, 1);

    // ── 空状态提示（初始显示） ────────────────────────────────────────────
    QLabel *emptyLb = new QLabel(QString::fromUtf8("暂无新的朋友请求"));
    emptyLb->setObjectName("fvp_empty_lb");
    emptyLb->setAlignment(Qt::AlignCenter);
    emptyLb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _layout->insertWidget(0, emptyLb);
    // 当有真实条目插入时，这个标签会被隐藏
    connect(this, &FriendVerifyPanel::sig_pending_changed, this,
            [emptyLb](int count) { emptyLb->setVisible(count == 0); });
}

void FriendVerifyPanel::AddRequest(const FriendRequest &req)
{
    // 已有则不重复添加
    if (_items.contains(req.uid)) return;

    auto *item = new FriendRequestItem(req, _content);
    _items[req.uid] = item;

    connect(item, &FriendRequestItem::sig_accepted, this, [this](int uid) {
        --_pending_count;
        emit sig_accepted(uid);
        emit sig_pending_changed(_pending_count);
    });
    connect(item, &FriendRequestItem::sig_rejected, this, [this](int uid) {
        --_pending_count;
        emit sig_rejected(uid);
        emit sig_pending_changed(_pending_count);
    });

    // 插到 stretch 之前（新请求在顶部）
    _layout->insertWidget(0, item);

    ++_pending_count;
    emit sig_pending_changed(_pending_count);
}
