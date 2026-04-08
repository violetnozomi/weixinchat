#ifndef CHAT_USER_LIST_H
#define CHAT_USER_LIST_H

#include <QEvent>
#include <QListWidget>
#include <QMap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include "chat_user_wid.h"

class Chat_User_List : public QListWidget
{
    Q_OBJECT
public:
    explicit Chat_User_List(QWidget *parent = nullptr);

    void AddChatUser(const QString &conversationKey, const QString &name, const QString &avatar = "",
                     const QString &lastMsg = "", const QString &time = "", int unread = 0);
    void AddChatUser(int uid, const QString &name, const QString &avatar = "",
                     const QString &lastMsg = "", const QString &time = "", int unread = 0)
    {
        AddChatUser(MakePeerConversationKey(uid), name, avatar, lastMsg, time, unread);
    }

    void UpdateLastMsg(const QString &conversationKey, const QString &msg, const QString &time);
    void UpdateLastMsg(int uid, const QString &msg, const QString &time)
    {
        UpdateLastMsg(MakePeerConversationKey(uid), msg, time);
    }

    void UpdateUnreadCount(const QString &conversationKey, int count);
    void UpdateUnreadCount(int uid, int count)
    {
        UpdateUnreadCount(MakePeerConversationKey(uid), count);
    }

    void UpdateChatUserInfo(const QString &conversationKey, const QString &name,
                            const QString &avatar = QString());

    void SetSelectChatUser(const QString &conversationKey);
    void SetSelectChatUser(int uid) { SetSelectChatUser(MakePeerConversationKey(uid)); }

    void ClearAllItems();
    void SetHasMoreData(bool has_more);
    bool IsLoading() const { return _is_loading; }
    void SetLoading(bool loading);
    void RefreshLayout();

    ChatUserWid *GetChatUserWid(const QString &conversationKey) const;
    ChatUserWid *GetChatUserWid(int uid) const
    {
        return GetChatUserWid(MakePeerConversationKey(uid));
    }

    void MoveToTop(const QString &conversationKey);
    void MoveToTop(int uid) { MoveToTop(MakePeerConversationKey(uid)); }

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QMap<QString, ChatUserWid*> _chat_user_map;
    ChatUserWid *_selected_user = nullptr;
    bool _has_more_data = true;
    bool _is_loading = false;

signals:
    void sig_loading_chat_user();
    void sig_chat_user_clicked(QString conversationKey);
};

#endif // CHAT_USER_LIST_H
