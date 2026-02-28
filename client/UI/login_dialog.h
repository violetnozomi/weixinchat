#ifndef LOGIN_DIALOG_H
#define LOGIN_DIALOG_H

#include <QDialog>
#include<QJsonObject>
#include<httpmgr.h>
#include<QMap>
#include<tcpmgr.h>
namespace Ui {
class Login_dialog;
}

class Login_dialog : public QDialog
{
    Q_OBJECT

public:
    explicit Login_dialog(QWidget *parent = nullptr);
    ~Login_dialog();

private:
    Ui::Login_dialog *ui;
    bool checkUserValid();
    bool checkPwdValid();
    void showTip(QString tip ,bool b_ok);
    QMap<REQ_ID , std::function<void(const QJsonObject&)>> _handlers;
    void initHandlers();

signals:
    void switchRegister();
    void switchForget();
    void sig_connect_tcp(QString host, QString port, QString token, int uid, QString user);
private slots:
    void slot_forget_pwd();
    void on_Login_btn_clicked();
    void slot_login_mod_finish(REQ_ID id , QString res , ErrorCodes err);
};

#endif // LOGIN_DIALOG_H
