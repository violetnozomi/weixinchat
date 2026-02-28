#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "chatdialog.h"
#include<QVBoxLayout>
#include<QLabel>
#include<qtimer.h>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 1. 创建 QStackedWidget 并设为中心窗口
    _stacked_widget = new QStackedWidget(this);
    setCentralWidget(_stacked_widget);

    // 2. 创建你的页面对象
    _login_dialog = new Login_dialog(this);
    _register_dialog = new Register_dialog(this);
    _forget_dialog = new ForgetDialog(this);
    // ChatDialog 作为独立窗口，不设置父对象
    _chat_dialog = new chatDialog(nullptr);
    _tip_widget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(_tip_widget);
    layout->addStretch();
    QLabel* tip_label = new QLabel("恭喜！注册成功\n即将返回登录界面...", _tip_widget);
    layout->addStretch(); // 顶部的弹簧
    layout->addWidget(tip_label);
    layout->addStretch(); // 底部的弹簧

    QFont font = tip_label->font();
    font.setPointSize(16);
    tip_label->setFont(font);
    tip_label->setAlignment(Qt::AlignCenter); // 文字居中
    layout->addWidget(tip_label);
    // 3. 【关键】把它们加入到 StackedWidget 中
    // addWidget 会按照顺序分配索引：Login是0，Register是1
    // ChatDialog 不再添加到 StackedWidget，作为独立窗口
    _stacked_widget->addWidget(_login_dialog);       // Index 0
    _stacked_widget->addWidget(_register_dialog);    // Index 1
    _stacked_widget->addWidget(_forget_dialog);      // Index 2
    _stacked_widget->addWidget(_tip_widget);         // Index 3

    // 4. 设置默认显示登录页
    _stacked_widget->setCurrentWidget(_login_dialog);

    // 5. 去掉之前的 setWindowFlags 代码
    // 因为它们现在是子控件，不需要设置无边框，它们会自动填满 StackedWidget

    // 6. 连接信号
    // 登录页 -> 切换到注册
    connect(_login_dialog, &Login_dialog::switchRegister, this, &MainWindow::handle_Register);
    connect(_register_dialog,&Register_dialog::Register_success , this , &MainWindow::handle_RegisterSuccess);
    connect(_login_dialog,&Login_dialog::switchForget,this,&MainWindow::handle_ForGetPwd);
    connect(_forget_dialog , &ForgetDialog::switch_Login_dialog , this , &MainWindow::handle_Cancel_ForGetPwd);
    // (可选) 注册页 -> 切换回登录 (假设你的注册页发出了 switchLogin 信号)
     connect(_register_dialog, &Register_dialog::switch_Login_Dialog, this, &MainWindow::handle_Login);
     
    // 连接登录成功信号到TcpMgr
    connect(_login_dialog, &Login_dialog::sig_connect_tcp, 
            TcpMgr::GetInstance().get(), &TcpMgr::slot_tcp_connect);
    
    // 连接TcpMgr的状态信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_connect_success, this, [this](){
        qDebug() << "TCP connection established, authenticating...";
    });
    
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_auth_success,
            this, &MainWindow::slot_switch_chatdlg);
    
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_auth_failed, this, [this](QString error){
        qDebug() << "Authentication failed:" << error;
        // 显示错误提示并返回登录界面
        _stacked_widget->setCurrentWidget(_login_dialog);
    });
    
    // TODO: 连接ChatDialog的发送消息信号到TcpMgr
    // connect(_chat_dialog, &chatDialog::sig_send_text_msg, this, [this](int to_uid, QString content){
    //     QJsonObject msgObj;
    //     msgObj["to_uid"] = to_uid;
    //     msgObj["content"] = content;
    //     TcpMgr::GetInstance()->SendMsg(ID_TEXT_CHAT_MSG, msgObj);
    // });
    
    // TODO: 连接TcpMgr的接收消息信号到ChatDialog
    // connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_text_msg_received,
    //         _chat_dialog, &chatDialog::slot_text_msg_received);
    
    // 连接ChatDialog关闭信号
    connect(_chat_dialog, &QDialog::finished, this, &MainWindow::slot_chat_dialog_closed);

}

MainWindow::~MainWindow()
{
    delete ui;
}

// 切换到注册页
void MainWindow::handle_RegisterSuccess(){
    _stacked_widget->setCurrentWidget(_tip_widget);
    QTimer::singleShot(3000, this, [=](){
        _stacked_widget->setCurrentWidget(_login_dialog);
    });
}
void MainWindow::handle_Register(){
    // 只需要这一行，自动切换界面
    _stacked_widget->setCurrentWidget(_register_dialog);

}
void MainWindow::handle_ForGetPwd(){
    _stacked_widget->setCurrentWidget(_forget_dialog);
}


// 切换到聊天界面
void MainWindow::slot_switch_chatdlg()
{
    qDebug() << "Authentication successful! Switching to chat dialog...";
    
    // TODO: 从TcpMgr获取用户信息并设置到chatDialog
    // auto tcpMgr = TcpMgr::GetInstance();
    // _chat_dialog->SetUserInfo(tcpMgr->GetUid(), tcpMgr->GetUser(), tcpMgr->GetToken());
    
    // TODO: 从服务器加载好友列表
    // 现在使用chatDialog内置的测试数据
    
    // 隐藏主窗口，显示独立的聊天窗口
    this->hide();
    _chat_dialog->show();
    _chat_dialog->raise();  // 确保窗口在最前面
    _chat_dialog->activateWindow();  // 激活窗口
}
void MainWindow::handle_Cancel_ForGetPwd()
{
    _stacked_widget->setCurrentWidget(_login_dialog);
}
// (可选) 切换回登录页
void MainWindow::handle_Login(){
    _stacked_widget->setCurrentWidget(_login_dialog);
}

// 处理聊天窗口关闭
void MainWindow::slot_chat_dialog_closed()
{
    qDebug() << "Chat dialog closed, returning to login...";
    
    // TODO: 如果需要，这里可以断开TCP连接
    // TcpMgr::GetInstance()->DisConnect();
    
    // 显示主窗口的登录界面
    _stacked_widget->setCurrentWidget(_login_dialog);
    this->show();
    this->raise();
    this->activateWindow();
}
