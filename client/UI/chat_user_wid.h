#ifndef CHAT_USER_WID_H
#define CHAT_USER_WID_H

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "MessageInfo.h"
#include "clickedbtn.h"

class ChatUserWid : public QWidget
{
    Q_OBJECT
public:
    explicit ChatUserWid(QWidget *parent = nullptr);
    ~ChatUserWid();

    void SetInfo(const QString &conversationKey, const QString &name, const QString &avatar = "");
    void SetInfo(int uid, const QString &name, const QString &avatar = "")
    {
        SetInfo(MakePeerConversationKey(uid), name, avatar);
    }

    void SetLastMsg(const QString &msg, const QString &time = "");
    void SetUnreadCount(int count);
    void SetSelected(bool selected);

    QString GetConversationKey() const { return _conversation_key; }
    int GetUid() const { return ExtractPeerUidFromConversationKey(_conversation_key); }
    QString GetName() const { return _name; }
    QString GetAvatarPath() const { return _avatar_path; }
    QString GetLastMsg() const { return _last_msg; }
    QString GetLastMsgTime() const { return _time; }
    int GetUnreadCount() const { return _unread_count; }

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

signals:
    void sig_clicked(QString conversationKey);

private:
    void setupUI();

    QString _conversation_key;
    QString _name;
    QString _avatar_path;
    QString _last_msg;
    QString _time;
    int _unread_count = 0;
    bool _selected = false;
    bool _hovered = false;

    QLabel *_avatar_label = nullptr;
    QLabel *_name_label = nullptr;
    QLabel *_last_msg_label = nullptr;
    QLabel *_time_label = nullptr;
    QLabel *_unread_label = nullptr;
};

#endif // CHAT_USER_WID_H
