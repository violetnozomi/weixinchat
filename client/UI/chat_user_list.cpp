#include "chat_user_list.h"

#include <QDebug>

Chat_User_List::Chat_User_List(QWidget *parent)
    : QListWidget(parent)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::NoFocus);
    setSpacing(0);
    setUniformItemSizes(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(0);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        auto *scrollBar = verticalScrollBar();
        if (!scrollBar) {
            return;
        }
        if (scrollBar->maximum() > 0 && value >= scrollBar->maximum() &&
            _has_more_data && !_is_loading) {
            _is_loading = true;
            emit sig_loading_chat_user();
        }
    });
}

void Chat_User_List::AddChatUser(const QString &conversationKey, const QString &name,
                                 const QString &avatar, const QString &lastMsg,
                                 const QString &time, int unread)
{
    if (_chat_user_map.contains(conversationKey)) {
        UpdateChatUserInfo(conversationKey, name, avatar);
        UpdateLastMsg(conversationKey, lastMsg, time);
        UpdateUnreadCount(conversationKey, unread);
        return;
    }

    auto *userWid = new ChatUserWid(this);
    userWid->SetInfo(conversationKey, name, avatar);
    userWid->SetLastMsg(lastMsg, time);
    userWid->SetUnreadCount(unread);

    connect(userWid, &ChatUserWid::sig_clicked, this, [this](const QString &key) {
        SetSelectChatUser(key);
        emit sig_chat_user_clicked(key);
    });

    auto *item = new QListWidgetItem(this);
    item->setSizeHint(QSize(0, 60));
    addItem(item);
    setItemWidget(item, userWid);
    viewport()->update();

    _chat_user_map[conversationKey] = userWid;
}

void Chat_User_List::UpdateLastMsg(const QString &conversationKey, const QString &msg, const QString &time)
{
    if (_chat_user_map.contains(conversationKey)) {
        _chat_user_map[conversationKey]->SetLastMsg(msg, time);
    }
}

void Chat_User_List::UpdateUnreadCount(const QString &conversationKey, int count)
{
    if (_chat_user_map.contains(conversationKey)) {
        _chat_user_map[conversationKey]->SetUnreadCount(count);
    }
}

void Chat_User_List::UpdateChatUserInfo(const QString &conversationKey, const QString &name,
                                        const QString &avatar)
{
    if (_chat_user_map.contains(conversationKey)) {
        const QString nextAvatar = avatar.isNull()
            ? _chat_user_map[conversationKey]->GetAvatarPath()
            : avatar;
        _chat_user_map[conversationKey]->SetInfo(conversationKey, name, nextAvatar);
    }
}

void Chat_User_List::SetSelectChatUser(const QString &conversationKey)
{
    if (_selected_user) {
        _selected_user->SetSelected(false);
    }

    if (_chat_user_map.contains(conversationKey)) {
        _selected_user = _chat_user_map[conversationKey];
        _selected_user->SetSelected(true);
        _selected_user->SetUnreadCount(0);
    }
}

void Chat_User_List::ClearAllItems()
{
    clear();
    _chat_user_map.clear();
    _selected_user = nullptr;
}

void Chat_User_List::SetHasMoreData(bool has_more)
{
    _has_more_data = has_more;
}

void Chat_User_List::SetLoading(bool loading)
{
    _is_loading = loading;
}

void Chat_User_List::RefreshLayout()
{
    doItemsLayout();
    updateGeometries();
    viewport()->update();
}

void Chat_User_List::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    viewport()->update();
}

void Chat_User_List::wheelEvent(QWheelEvent *event)
{
    QListWidget::wheelEvent(event);

    auto *scrollBar = verticalScrollBar();
    if (!scrollBar || !event) {
        return;
    }

    const bool scrollingDown = event->angleDelta().y() < 0;
    const bool atBottom = scrollBar->value() >= scrollBar->maximum();
    if (scrollingDown && atBottom && _has_more_data && !_is_loading) {
        _is_loading = true;
        emit sig_loading_chat_user();
    }
}

ChatUserWid *Chat_User_List::GetChatUserWid(const QString &conversationKey) const
{
    return _chat_user_map.value(conversationKey, nullptr);
}

void Chat_User_List::MoveToTop(const QString &conversationKey)
{
    if (!_chat_user_map.contains(conversationKey)) {
        return;
    }

    ChatUserWid *wid = _chat_user_map[conversationKey];
    if (!wid) {
        return;
    }

    const QString name = wid->GetName();
    const QString avatar = wid->GetAvatarPath();
    const QString lastMsg = wid->GetLastMsg();
    const QString time = wid->GetLastMsgTime();
    const int unread = wid->GetUnreadCount();
    const bool wasSelected = (_selected_user == wid);

    int row = -1;
    for (int i = 0; i < count(); ++i) {
        if (itemWidget(item(i)) == wid) {
            row = i;
            break;
        }
    }
    if (row <= 0) {
        return;
    }

    QListWidgetItem *currentItem = item(row);
    if (!currentItem) {
        return;
    }

    setUpdatesEnabled(false);
    removeItemWidget(currentItem);
    QListWidgetItem *oldItem = takeItem(row);
    if (!oldItem) {
        setUpdatesEnabled(true);
        return;
    }
    delete oldItem;

    _chat_user_map.remove(conversationKey);
    if (_selected_user == wid) {
        _selected_user = nullptr;
    }
    wid->deleteLater();

    auto *newWid = new ChatUserWid(this);
    newWid->SetInfo(conversationKey, name, avatar);
    newWid->SetLastMsg(lastMsg, time);
    newWid->SetUnreadCount(unread);
    connect(newWid, &ChatUserWid::sig_clicked, this, [this](const QString &key) {
        SetSelectChatUser(key);
        emit sig_chat_user_clicked(key);
    });

    auto *newItem = new QListWidgetItem();
    newItem->setSizeHint(QSize(0, 60));
    insertItem(0, newItem);
    setItemWidget(newItem, newWid);
    _chat_user_map[conversationKey] = newWid;

    if (wasSelected) {
        _selected_user = newWid;
        _selected_user->SetSelected(true);
        _selected_user->SetUnreadCount(0);
    }

    setUpdatesEnabled(true);
    viewport()->update();
}
