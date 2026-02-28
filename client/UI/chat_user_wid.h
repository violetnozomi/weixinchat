#ifndef CHAT_USER_WID_H
#define CHAT_USER_WID_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include "clickedbtn.h"

/**
 * @brief 好友列表项Widget，仿微信样式
 * 显示头像、用户名、最后一条消息、时间、未读消息数
 */
class ChatUserWid : public QWidget
{
    Q_OBJECT
public:
    explicit ChatUserWid(QWidget *parent = nullptr);
    ~ChatUserWid();
    
    /**
     * @brief 设置用户信息
     * @param uid 用户ID
     * @param name 用户名
     * @param avatar 头像路径
     */
    void SetInfo(int uid, const QString& name, const QString& avatar = "");
    
    /**
     * @brief 设置最后一条消息
     * @param msg 消息内容
     * @param time 消息时间
     */
    void SetLastMsg(const QString& msg, const QString& time = "");
    
    /**
     * @brief 设置未读消息数
     * @param count 未读消息数，0表示无未读
     */
    void SetUnreadCount(int count);
    
    /**
     * @brief 获取用户ID
     */
    int GetUid() const { return _uid; }
    
    /**
     * @brief 获取用户名
     */
    QString GetName() const { return _name; }

    /**
     * @brief 获取头像路径
     */
    QString GetAvatarPath() const { return _avatar_path; }
    
    /**
     * @brief 设置选中状态
     */
    void SetSelected(bool selected);

protected:
    /**
     * @brief 鼠标进入事件
     */
    void enterEvent(QEnterEvent *event) override;
    
    /**
     * @brief 鼠标离开事件
     */
    void leaveEvent(QEvent *event) override;
    
    /**
     * @brief 鼠标按下事件
     */
    void mousePressEvent(QMouseEvent *event) override;
    
    /**
     * @brief 绘制事件
     */
    void paintEvent(QPaintEvent *event) override;

signals:
    void sig_clicked(int uid);  // 点击信号

private:
    void setupUI();  // 初始化UI

private:
    int _uid;              // 用户ID
    QString _name;         // 用户名
    QString _avatar_path;  // 头像路径
    bool _selected;        // 是否选中
    bool _hovered;         // 鼠标是否悬停
    
    // UI组件
    QLabel* _avatar_label;      // 头像
    QLabel* _name_label;        // 用户名
    QLabel* _last_msg_label;    // 最后一条消息
    QLabel* _time_label;        // 时间
    QLabel* _unread_label;      // 未读消息数
};

#endif // CHAT_USER_WID_H
