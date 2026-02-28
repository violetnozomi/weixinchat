#ifndef CONTACTLISTPANEL_H
#define CONTACTLISTPANEL_H

#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QMap>
#include "FriendInfo.h"

// ── 左侧联系人列表面板（ContactMode）──────────────────────────────────────
class ContactListPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ContactListPanel(QWidget *parent = nullptr);

    // 添加好友条目
    void AddFriend(const FriendInfo &info);

    // 移除指定好友条目
    void RemoveFriend(int uid);

    // 清空所有好友条目（保留"新的朋友"行）
    void ClearFriends();

    // 设置"新的朋友"徽标数字（0 = 不显示）
    void SetPendingCount(int count);

signals:
    void sig_new_friends_clicked();
    void sig_friend_clicked(int uid, const QString &name,
                            const QString &avatarPath, const QString &signature);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel      *_badge_lb;
    int          _pending_count = 0;

    QScrollArea *_scroll;
    QWidget     *_content;
    QVBoxLayout *_friend_layout;   // 好友行插入位置（在 section header 之后）

    QMap<int, QWidget*> _friend_items;
};

#endif // CONTACTLISTPANEL_H
