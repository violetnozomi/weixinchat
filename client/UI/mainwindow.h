#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget> // 1. 引入头文件
#include "login_dialog.h"
#include "register_dialog.h"
#include "forgetdialog.h"
#include "chatdialog.h"
#include "tcpmgr.h"
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void handle_Register();
    void handle_Login(); // 建议增加这个，用于从注册页返回登录页
    void handle_RegisterSuccess();
    void handle_ForGetPwd();
    void slot_switch_chatdlg();  // 切换到聊天界面
    void handle_Cancel_ForGetPwd();
    void slot_chat_dialog_closed();  // 处理聊天窗口关闭
private:
    Ui::MainWindow *ui;

    // 2. 声明核心控件
    QStackedWidget* _stacked_widget;
    Login_dialog* _login_dialog;
    Register_dialog* _register_dialog;
    chatDialog* _chat_dialog;
    ForgetDialog* _forget_dialog;
    QWidget* _tip_widget;
};

#endif // MAINWINDOW_H
