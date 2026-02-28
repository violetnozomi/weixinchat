#include "chat_user_list.h"
#include <QDebug>

Chat_User_List::Chat_User_List(QWidget* parent)
    : QListWidget(parent)
    , _selected_user(nullptr)
    , _has_more_data(true)
    , _is_loading(false)
{
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    // 基础样式由QSS文件控制，这里只设置必要的属性
    this->setFocusPolicy(Qt::NoFocus);
    this->setSpacing(0);
    this->setUniformItemSizes(true);
    this->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    
    // 设置大小策略，确保列表能够扩展填充空间
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->setMinimumHeight(0);

    connect(this->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar* scrollBar = this->verticalScrollBar();
        if (!scrollBar) {
            return;
        }
        if (scrollBar->maximum() > 0 && value >= scrollBar->maximum() && _has_more_data && !_is_loading) {
            qDebug() << "Reached bottom, loading more chat users...";
            _is_loading = true;
            emit sig_loading_chat_user();
        }
    });
}

void Chat_User_List::AddChatUser(int uid, const QString& name, const QString& avatar,
                                 const QString& last_msg, const QString& time, int unread)
{
    // 检查是否已存在
    if (_chat_user_map.contains(uid)) {
        qDebug() << "User" << uid << "already exists in list";
        return;
    }
    
    // 创建自定义Widget
    ChatUserWid* user_wid = new ChatUserWid(this);
    user_wid->SetInfo(uid, name, avatar);
    user_wid->SetLastMsg(last_msg, time);
    user_wid->SetUnreadCount(unread);
    
    // 连接点击信号
    connect(user_wid, &ChatUserWid::sig_clicked, this, [this](int uid) {
        SetSelectChatUser(uid);
        emit sig_chat_user_clicked(uid);
    });
    
    // 添加到列表
    QListWidgetItem* item = new QListWidgetItem(this);
    item->setSizeHint(QSize(0, 60));
    this->addItem(item);
    this->setItemWidget(item, user_wid);
    this->viewport()->update();
    
    // 保存映射
    _chat_user_map[uid] = user_wid;
}

void Chat_User_List::UpdateLastMsg(int uid, const QString& msg, const QString& time)
{
    if (_chat_user_map.contains(uid)) {
        _chat_user_map[uid]->SetLastMsg(msg, time);
        
        // TODO: 可以在这里实现消息排序，把最新消息的用户移到最上面
    }
}

void Chat_User_List::UpdateUnreadCount(int uid, int count)
{
    if (_chat_user_map.contains(uid)) {
        _chat_user_map[uid]->SetUnreadCount(count);
    }
}

void Chat_User_List::SetSelectChatUser(int uid)
{
    // 取消之前的选中
    if (_selected_user) {
        _selected_user->SetSelected(false);
    }
    
    // 设置新的选中
    if (_chat_user_map.contains(uid)) {
        _selected_user = _chat_user_map[uid];
        _selected_user->SetSelected(true);
        
        // 清除未读消息数
        _selected_user->SetUnreadCount(0);
    }
}

void Chat_User_List::ClearAllItems()
{
    this->clear();
    _chat_user_map.clear();
    _selected_user = nullptr;
}

void Chat_User_List::SetHasMoreData(bool has_more)
{
    _has_more_data = has_more;
    qDebug() << "Has more data:" << has_more;
}

void Chat_User_List::SetLoading(bool loading)
{
    _is_loading = loading;
    qDebug() << "Loading state:" << loading;
}

void Chat_User_List::RefreshLayout()
{
    this->doItemsLayout();
    this->updateGeometries();
    this->viewport()->update();
}

void Chat_User_List::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    this->viewport()->update();
}

void Chat_User_List::wheelEvent(QWheelEvent *event)
{
    QListWidget::wheelEvent(event);

    if (!event) {
        return;
    }

    QScrollBar* scrollBar = this->verticalScrollBar();
    if (!scrollBar) {
        return;
    }

    const bool scrollingDown = event->angleDelta().y() < 0;
    const bool atBottom = scrollBar->value() >= scrollBar->maximum();

    if (scrollingDown && atBottom && _has_more_data && !_is_loading) {
        qDebug() << "Wheel reached bottom, loading more chat users...";
        _is_loading = true;
        emit sig_loading_chat_user();
    }
}

ChatUserWid* Chat_User_List::GetChatUserWid(int uid) const
{
    return _chat_user_map.value(uid, nullptr);
}
