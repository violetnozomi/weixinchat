#include "login_dialog.h"
#include "ui_login_dialog.h"
#include <QCryptographicHash>

Login_dialog::Login_dialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Login_dialog)
{
    ui->setupUi(this);
    ui->Register_label->setCursor(Qt::PointingHandCursor);
    ui->Register_label->SetState("normal","hover","","selected","selected_hover","");
    connect(ui->Register_label,&ClickedLable::clicked,this,&Login_dialog::switchRegister);
    ui->forget_lable->SetState("normal","hover","","selected","selected_hover","");
    connect(ui->forget_lable,&ClickedLable::clicked ,this ,&Login_dialog::slot_forget_pwd);
    ui->pwd_edit->setEchoMode(QLineEdit::Password);
    
    // 设置密码可见性切换图标
    ui->pwd_visble->setCursor(Qt::PointingHandCursor);
    ui->pwd_visble->SetState("unvisible","unvisible_hover","","visible",
                              "visible_hover","");
    
    // 连接密码可见性切换事件
    connect(ui->pwd_visble, &ClickedLable::clicked, this, [this](){
        auto cur_state = ui->pwd_visble->GetCurState();
        if(cur_state == ClickLbState::Normal){
            ui->pwd_edit->setEchoMode(QLineEdit::Password);
        }else{
            ui->pwd_edit->setEchoMode(QLineEdit::Normal);
        }
    });
    connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_login_mod_finish, this,
            &Login_dialog::slot_login_mod_finish);
    // 注意：sig_connect_tcp信号的连接已移至MainWindow，避免重复连接
    ui->err_tip->clear();
    
    // 初始化HTTP响应处理器
    initHandlers();
}


Login_dialog::~Login_dialog()
{
    delete ui;
}
void Login_dialog::initHandlers(){
    _handlers.insert(REQ_ID::ID_LOGIN_USER,[this](QJsonObject jsonobj){
        qDebug() << "[LOGIN] Handler called with JSON:" << jsonobj;
        int err = jsonobj["error"].toInt();
        qDebug() << "[LOGIN] Error code:" << err;
        
        if(err != ErrorCodes::Success){
            qDebug() << "[LOGIN] Login failed with error:" << err;
            // 根据不同的错误码显示不同的提示
            if(err == ErrorCodes::PasswdInvalid){
                showTip(tr("用户不存在或密码错误"),false);
            }else if(err == ErrorCodes::Error_Json){
                showTip(tr("参数错误"),false);
            }else{
                showTip(tr("登录失败"),false);
            }
            return;
        }
        
        // 登录成功，获取返回的用户信息
        auto user = jsonobj["user"].toString();
        auto uid = jsonobj["uid"].toInt();
        auto token = jsonobj["token"].toString();
        auto host = jsonobj["host"].toString();
        auto port = jsonobj["port"].toString();
        
        qDebug() << "[LOGIN] Login successful!";
        qDebug() << "[LOGIN]   User:" << user;
        qDebug() << "[LOGIN]   UID:" << uid;
        qDebug() << "[LOGIN]   Token:" << token;
        qDebug() << "[LOGIN]   ChatServer:" << host << ":" << port;
        
        showTip(tr("登录成功，正在连接聊天服务器..."), true);
        
        // 发送信号，通知TcpMgr连接ChatServer
        qDebug() << "[LOGIN] Emitting sig_connect_tcp signal";
        emit sig_connect_tcp(host, port, token, uid, user);
    });


}
void Login_dialog::slot_login_mod_finish(REQ_ID id , QString res , ErrorCodes err){
    qDebug() << "[LOGIN] slot_login_mod_finish called, error:" << (int)err;
    qDebug() << "[LOGIN] Response:" << res;
    
    if(err != ErrorCodes::Success){
        qDebug() << "[LOGIN] Network error occurred";
        showTip(tr("网络请求错误"),false);
        return;
    }

    // 解析 JSON 字符串,res需转化为QByteArray
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    //json解析错误
    if(jsonDoc.isNull()){
        qDebug() << "[LOGIN] JSON parsing failed - null document";
        showTip(tr("json解析错误"),false);
        return;
    }

    //json解析错误
    if(!jsonDoc.isObject()){
        qDebug() << "[LOGIN] JSON parsing failed - not an object";
        showTip(tr("json解析错误"),false);
        return;
    }
    qDebug() << "[LOGIN] Calling handler for request ID:" << (int)id;
    _handlers[id](jsonDoc.object());

}
void Login_dialog::showTip(QString tip ,bool b_ok){
    if(b_ok)
    {
        ui->err_tip->setProperty("state","normal");
    }
    else{
        ui->err_tip->setProperty("state","err");
    }
    ui->err_tip->setText(tip);
    repolish(ui->err_tip);
}


void Login_dialog::slot_forget_pwd()
{
    emit this->switchForget();
}
bool Login_dialog::checkUserValid(){

    auto user = ui->user_edit->text();
    if(user.isEmpty()){
        qDebug() << "User empty " ;
        return false;
    }

    return true;
}
bool Login_dialog::checkPwdValid(){
    auto pwd = ui->pwd_edit->text();
    if(pwd.length() < 6 || pwd.length() > 15){
        qDebug() << "Pass length invalid";
        return false;
    }

    return true;
}
void Login_dialog::on_Login_btn_clicked()
{
    qDebug()<<"[LOGIN] login btn clicked";
    if(checkUserValid() == false){
        qDebug() << "[LOGIN] User validation failed";
        showTip(tr("请输入用户名"), false);
        return;
    }

    if(checkPwdValid() == false){
        qDebug() << "[LOGIN] Password validation failed";
        showTip(tr("密码长度应为6-15位"), false);
        return ;
    }
    // 客户端预哈希：SHA-256(明文密码)，防止明文密码在网络中传输
    auto sha256 = [](const QString& s) -> QString {
        return QString(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
    };

    QJsonObject jsonobj;
    jsonobj["user"] = ui->user_edit->text();
    jsonobj["pwd"] = sha256(ui->pwd_edit->text());
    qDebug() << "[LOGIN] Sending HTTP login request to:" << gate_url_prefix+"/user_login";
    qDebug() << "[LOGIN] User:" << ui->user_edit->text();
    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix+"/user_login"),jsonobj,REQ_ID::ID_LOGIN_USER,Modules::USERLOGIN);

}

