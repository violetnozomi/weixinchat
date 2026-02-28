#ifndef SEARCHUSERITEM_H
#define SEARCHUSERITEM_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>

// 搜索结果中的单条用户项
class SearchUserItem : public QWidget
{
    Q_OBJECT
public:
    enum class State { Normal, AlreadyFriend };

    explicit SearchUserItem(int uid,
                            const QString &name,
                            const QString &wechatId,
                            const QString &avatarPath,
                            State state = State::Normal,
                            QWidget *parent = nullptr);

    int     GetUid()  const { return _uid; }
    QString GetName() const { return _name; }
    QString GetAvatarPath() const { return _avatarPath; }

signals:
    void sig_add_clicked(int uid, const QString &name, const QString &avatarPath);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int     _uid;
    QString _name;
    QString _avatarPath;
    bool    _hovered = false;

    QLabel      *_avatar_lb;
    QLabel      *_name_lb;
    QLabel      *_id_lb;
    QPushButton *_add_btn;
};

#endif // SEARCHUSERITEM_H
