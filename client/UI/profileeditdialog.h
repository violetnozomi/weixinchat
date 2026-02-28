#ifndef PROFILEEDITDIALOG_H
#define PROFILEEDITDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>

// ── 用户个人资料编辑对话框 ─────────────────────────────────────────────────
// 允许用户修改：头像（点击更换）、昵称、个性签名
// 点击「保存」后 accept()；调用方读取数据并发送 TCP 请求。
class ProfileEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProfileEditDialog(const QString &name,
                               const QString &signature,
                               const QPixmap  &avatar,
                               QWidget *parent = nullptr);

    // ── 读取修改后的值 ───────────────────────────────────────────────────
    QString GetName()      const;
    QString GetSignature() const;
    QPixmap GetAvatar()    const { return _newAvatar; }
    bool    AvatarChanged()const { return _avatarChanged; }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void updateAvatarLabel();

    QLabel    *_avatarLb;
    QLineEdit *_nameEdit;
    QLineEdit *_sigEdit;

    QPixmap   _newAvatar;
    bool      _avatarChanged = false;
};

#endif // PROFILEEDITDIALOG_H
