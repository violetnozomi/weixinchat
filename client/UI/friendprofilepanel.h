#ifndef FRIENDPROFILEPANEL_H
#define FRIENDPROFILEPANEL_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>

// ── 右侧好友资料卡（点击联系人时展示）───────────────────────────────────
class FriendProfilePanel : public QWidget
{
    Q_OBJECT
public:
    explicit FriendProfilePanel(QWidget *parent = nullptr);

    // 切换展示的好友
    void ShowFriend(int uid, const QString &name,
                    const QString &avatarPath,
                    const QString &signature = "");

    // 重置为空白提示（没有选中任何好友时）
    void Reset();

signals:
    void sig_send_message(int uid, const QString &name, const QString &avatarPath);
    void sig_delete_friend(int uid);    // 删除好友
    void sig_block_friend (int uid);    // 屏蔽好友

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int         _cur_uid = -1;
    QString     _cur_name;
    QString     _cur_avatar;

    QLabel      *_avatar_lb;
    QLabel      *_name_lb;
    QLabel      *_uid_lb;
    QLabel      *_sig_lb;
    QPushButton *_msg_btn;
    QPushButton *_delete_btn;    // 删除好友
    QPushButton *_block_btn;     // 屏蔽好友
    QWidget     *_info_widget;  // 整体信息块，方便显隐
    QLabel      *_empty_lb;     // 空状态提示
};

#endif // FRIENDPROFILEPANEL_H
