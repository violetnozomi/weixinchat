#ifndef CHAT_USER_LIST_H
#define CHAT_USER_LIST_H
#include<QListWidget>
#include<QEvent>
#include<QWheelEvent>
#include<QScrollBar>
#include<QMap>
#include"chat_user_wid.h"

class Chat_User_List:public QListWidget
{
     Q_OBJECT
public:
    Chat_User_List(QWidget* parent = nullptr);
    
    /**
     * @brief 添加好友项
     */
    void AddChatUser(int uid, const QString& name, const QString& avatar = "",
                     const QString& last_msg = "", const QString& time = "", int unread = 0);
    
    /**
     * @brief 更新好友最后一条消息
     */
    void UpdateLastMsg(int uid, const QString& msg, const QString& time);
    
    /**
     * @brief 更新未读消息数
     */
    void UpdateUnreadCount(int uid, int count);
    
    /**
     * @brief 设置选中的好友
     */
    void SetSelectChatUser(int uid);
    
    /**
     * @brief 清空列表
     */
    void ClearAllItems();
    
    /**
     * @brief 设置是否还有更多数据
     */
    void SetHasMoreData(bool has_more);
    
    /**
     * @brief 获取是否正在加载
     */
    bool IsLoading() const { return _is_loading; }
    
    /**
     * @brief 设置加载状态
     */
    void SetLoading(bool loading);

    /**
     * @brief 强制刷新列表布局（用于首次显示）
     */
    void RefreshLayout();

    /**
     * @brief 获取指定 uid 对应的 ChatUserWid，未找到返回 nullptr
     */
    ChatUserWid* GetChatUserWid(int uid) const;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
     
private:
    QMap<int, ChatUserWid*> _chat_user_map;  // uid -> ChatUserWid*
    ChatUserWid* _selected_user;             // 当前选中的用户
    bool _has_more_data;                     // 是否还有更多数据
    bool _is_loading;                        // 是否正在加载
    
signals:
     void sig_loading_chat_user();  // 滚动到底部，请求加载更多
     void sig_chat_user_clicked(int uid);  // 点击好友项
};

#endif // CHAT_USER_LIST_H
