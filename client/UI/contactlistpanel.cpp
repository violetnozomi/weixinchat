#include "contactlistpanel.h"
#include "chatbubbleitem.h"   // makeRoundAvatar
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <QScrollBar>
#include <functional>

// ── 通用可点击行基类 ───────────────────────────────────────────────────────
class ClickableRow : public QWidget
{
public:
    using Cb = std::function<void()>;
    explicit ClickableRow(Cb cb, QWidget *parent = nullptr)
        : QWidget(parent), _cb(cb)
    {
        setFixedHeight(60);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
    }
protected:
    bool _hovered = false;
    void enterEvent(QEnterEvent *) override { _hovered = true;  update(); }
    void leaveEvent(QEvent *)      override { _hovered = false; update(); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), _hovered ? QColor(0xE8,0xE8,0xE8) : QColor(0xF7,0xF7,0xF7));
        p.setPen(QColor(0xE0,0xE0,0xE0));
        p.drawLine(56, height()-1, width(), height()-1);
    }
    void mousePressEvent(QMouseEvent *e) override {
        QWidget::mousePressEvent(e);
        if (e->button() == Qt::LeftButton && rect().contains(e->pos())) _cb();
    }
private:
    Cb _cb;
};

// ── 分区标题行 ─────────────────────────────────────────────────────────────
static QWidget* makeSectionHeader(const QString &text, QWidget *parent)
{
    auto *w = new QWidget(parent);
    w->setObjectName("contact_section_header");
    w->setFixedHeight(32);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(14, 0, 14, 0);
    auto *lb = new QLabel(text, w);
    lb->setObjectName("contact_section_lb");
    h->addWidget(lb);
    return w;
}

// ─────────────────────────────────────────────────────────────────────────────

ContactListPanel::ContactListPanel(QWidget *parent)
    : QWidget(parent), _badge_lb(nullptr),
      _scroll(nullptr), _content(nullptr), _friend_layout(nullptr)
{
    setObjectName("ContactListPanel");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── 滚动区（包含所有内容）────────────────────────────────────────────
    _scroll = new QScrollArea(this);
    _scroll->setWidgetResizable(true);
    _scroll->setFrameShape(QFrame::NoFrame);
    _scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    _content = new QWidget();
    _content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *vbox = new QVBoxLayout(_content);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    // ── "新的朋友" 行 ─────────────────────────────────────────────────────
    auto *newFRow = new ClickableRow([this]() { emit sig_new_friends_clicked(); }, _content);
    {
        QHBoxLayout *h = new QHBoxLayout(newFRow);
        h->setContentsMargins(12, 0, 12, 0);
        h->setSpacing(12);

        QLabel *iconLb = new QLabel(newFRow);
        iconLb->setObjectName("contact_newf_icon");
        iconLb->setFixedSize(40, 40);
        iconLb->setAlignment(Qt::AlignCenter);
        {
            QPixmap px(":/res/add_friend_normal.png");
            if (!px.isNull())
                iconLb->setPixmap(px.scaled(22, 22, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }

        QLabel *textLb = new QLabel(QString::fromUtf8("新的朋友"), newFRow);
        textLb->setObjectName("contact_newf_text");

        _badge_lb = new QLabel(newFRow);
        _badge_lb->setObjectName("contact_badge_lb");
        _badge_lb->setAlignment(Qt::AlignCenter);
        _badge_lb->setFixedSize(20, 20);
        _badge_lb->hide();

        h->addWidget(iconLb);
        h->addWidget(textLb, 1);
        h->addWidget(_badge_lb);
    }
    vbox->addWidget(newFRow);

    // ── "朋友" 分区标题 ───────────────────────────────────────────────────
    vbox->addWidget(makeSectionHeader(QString::fromUtf8("朋友"), _content));

    // ── 好友列表插入位置（在 stretch 之前）────────────────────────────────
    _friend_layout = vbox;   // 直接复用，记录当前 count 为插入基准
    vbox->addStretch();

    _scroll->setWidget(_content);
    outer->addWidget(_scroll);
}

void ContactListPanel::AddFriend(const FriendInfo &info)
{
    if (_friend_items.contains(info.uid)) return;

    auto *row = new ClickableRow([this, info]() {
        emit sig_friend_clicked(info.uid, info.name, info.avatarPath, info.signature);
    }, _content);
    row->setObjectName("FriendRow");

    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(12);

    QLabel *avatarLb = new QLabel(row);
    avatarLb->setFixedSize(40, 40);
    avatarLb->setAlignment(Qt::AlignCenter);
    {
        QPixmap px(info.avatarPath.isEmpty() ? ":/res/head_1.jpg" : info.avatarPath);
        avatarLb->setPixmap(ChatBubbleItem::makeRoundAvatar(px, 40));
    }

    QVBoxLayout *tv = new QVBoxLayout();
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(3);

    QLabel *nameLb = new QLabel(info.name, row);
    nameLb->setObjectName("friend_row_name");

    tv->addWidget(nameLb);

    if (!info.signature.isEmpty()) {
        QLabel *sigLb = new QLabel(info.signature, row);
        sigLb->setObjectName("friend_row_sig");
        tv->addWidget(sigLb);
    }

    h->addWidget(avatarLb);
    h->addLayout(tv, 1);

    // 插入到 stretch 之前（stretch 始终在最后）
    int stretchIdx = _friend_layout->count() - 1;
    _friend_layout->insertWidget(stretchIdx, row);

    _friend_items[info.uid] = row;
}

void ContactListPanel::ClearFriends()
{
    for (auto *w : _friend_items) {
        _friend_layout->removeWidget(w);
        w->deleteLater();
    }
    _friend_items.clear();
}

void ContactListPanel::RemoveFriend(int uid)
{
    if (!_friend_items.contains(uid)) return;
    auto *w = _friend_items.take(uid);
    _friend_layout->removeWidget(w);
    w->deleteLater();
}

void ContactListPanel::SetPendingCount(int count)
{
    _pending_count = count;
    if (count <= 0) {
        _badge_lb->hide();
    } else {
        _badge_lb->setText(count > 99 ? "99+" : QString::number(count));
        _badge_lb->show();
    }
}

void ContactListPanel::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0xF7, 0xF7, 0xF7));
    QWidget::paintEvent(event);
}
