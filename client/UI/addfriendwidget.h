#ifndef ADDFRIENDWIDGET_H
#define ADDFRIENDWIDGET_H

#include <QDialog>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>

// 微信风格「添加好友」确认对话框
class AddFriendWidget : public QDialog
{
    Q_OBJECT
public:
    explicit AddFriendWidget(int uid,
                             const QString &name,
                             const QString &avatarPath,
                             QWidget *parent = nullptr);

signals:
    // 用户点击「发送」后发出，携带验证消息
    void sig_send_request(int uid, const QString &message);

private:
    int     _uid;
    QString _name;

    QLabel      *_avatar_lb;
    QLabel      *_name_lb;
    QLabel      *_hint_lb;
    QTextEdit   *_msg_edit;
    QPushButton *_cancel_btn;
    QPushButton *_send_btn;
};

#endif // ADDFRIENDWIDGET_H
