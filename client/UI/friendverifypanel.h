#ifndef FRIENDVERIFYPANEL_H
#define FRIENDVERIFYPANEL_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QMap>
#include "friendrequestitem.h"

// ── 右侧"新的朋友"验证面板 ─────────────────────────────────────────────────
class FriendVerifyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FriendVerifyPanel(QWidget *parent = nullptr);

    // 向列表顶部插入一条好友申请
    void AddRequest(const FriendRequest &req);

    int  PendingCount()  const { return _pending_count; }
    bool HasRequest(int uid) const { return _items.contains(uid); }

signals:
    void sig_accepted(int uid);
    void sig_rejected(int uid);
    void sig_pending_changed(int newCount);  // 未处理数量变化

private:
    int          _pending_count = 0;
    QScrollArea *_scroll;
    QWidget     *_content;
    QVBoxLayout *_layout;

    QMap<int, FriendRequestItem *> _items;  // uid → item
};

#endif // FRIENDVERIFYPANEL_H
