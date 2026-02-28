#include "register_dialog.h"
#include "ui_register_dialog.h"
#include "httpmgr.h"
#include <QCryptographicHash>
Register_dialog::Register_dialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Register_dialog)
{
    ui->setupUi(this);
    ui->pwd_edit->setEchoMode(QLineEdit::Password);
    ui->pwd_con_edit->setEchoMode(QLineEdit::Password);
    ui->err_tip->setProperty("state","normal");
    ui->err_tip->setText("");
    repolish(ui->err_tip);
    initHttpHandlers();
    qDebug() << "Debug: 准备获取 HttpMgr 实例...";
    auto ptr = HttpMgr::GetInstance(); // 先获取指针
    qDebug() << "Debug: 获取成功，地址是:" << ptr.get();

    if (ptr.get() == nullptr) {
        qDebug() << "Fatal: HttpMgr 是空的！";
        return;
    }
    ui->pwd_visble->setCursor(Qt::PointingHandCursor);
    ui->con_pwd_visble->setCursor(Qt::PointingHandCursor);
    ui->pwd_visble->SetState("unvisible","unvisible_hover","","visible",
                               "visible_hover","");

    ui->con_pwd_visble->SetState("unvisible","unvisible_hover","","visible",
                                  "visible_hover","");

    connect(ui->con_pwd_visble ,&ClickedLable::clicked,this,[this](){
        auto cur_state = ui->con_pwd_visble->GetCurState();
        if(cur_state == ClickLbState::Normal){
            ui->pwd_con_edit->setEchoMode(QLineEdit::Password);

        }else{
            ui->pwd_con_edit->setEchoMode(QLineEdit::Normal);
        }
    });
    connect(ui->pwd_visble ,&ClickedLable::clicked,this,[this](){
        auto cur_state = ui->pwd_visble->GetCurState();
        if(cur_state == ClickLbState::Normal){
            ui->pwd_edit->setEchoMode(QLineEdit::Password);

        }else{
            ui->pwd_edit->setEchoMode(QLineEdit::Normal);
        }
    });
    
    // 返回登录界面
    ui->return_login_label->setCursor(Qt::PointingHandCursor);
    ui->return_login_label->SetState("normal","hover","","selected","selected_hover","");
    connect(ui->return_login_label, &ClickedLable::clicked, this, [this](){
        emit switch_Login_Dialog();
    });
    
        qDebug() << "Debug: 准备连接 connect...";
    // 把原来的 connect 替换成下面这行（拆解开来）
    connect(ptr.get(), &HttpMgr::sig_reg_mod_finish,
            this, &Register_dialog::slot_reg_mod_finish);

    connect(ui->user_edit,&QLineEdit::editingFinished,this,[this](){
        checkUserValid();
    });

    connect(ui->emial_edit, &QLineEdit::editingFinished, this, [this](){
        checkEmailValid();
    });

    connect(ui->pwd_edit, &QLineEdit::editingFinished, this, [this](){
        checkPassValid();
    });

    connect(ui->pwd_con_edit, &QLineEdit::editingFinished, this, [this](){
        checkConfirmValid();
    });

    connect(ui->CAPTCHA_edit, &QLineEdit::editingFinished, this, [this](){
        checkVarifyValid();
    });
    qDebug() << "Debug: connect 成功！";

}
void Register_dialog::AddTipErr(TipErr te, QString tips)
{
    _tip_errs[te] = tips;
    showTip(tips, false);
}

void Register_dialog::DelTipErr(TipErr te)
{
    _tip_errs.remove(te);
    if(_tip_errs.empty()){
        ui->err_tip->clear();
        return;
    }

    showTip(_tip_errs.first(), false);
}
bool Register_dialog::checkUserValid()
{
    if(ui->user_edit->text() == ""){
        AddTipErr(TipErr::TIP_USER_ERR, tr("用户名不能为空"));
        return false;
    }

    DelTipErr(TipErr::TIP_USER_ERR);
    return true;
}

bool Register_dialog::checkConfirmValid(){
    auto confir_pwd = ui->pwd_con_edit->text();
    if(confir_pwd.length() < 6 || confir_pwd.length()>15){
        //提示长度不准确
        AddTipErr(TipErr::TIP_CONFIRM_ERR, tr("密码长度应为6~15"));
        return false;
    }
    if(confir_pwd != ui->pwd_edit->text())
    {
        AddTipErr(TipErr::TIP_CONFIRM_ERR, tr("确认密码与密码不同"));
        return false;
    }
    DelTipErr(TipErr::TIP_CONFIRM_ERR);
    return true;
}
bool Register_dialog::checkPassValid()
{
    auto pass = ui->pwd_edit->text();

    if(pass.length() < 6 || pass.length()>15){
        //提示长度不准确
        AddTipErr(TipErr::TIP_PWD_ERR, tr("密码长度应为6~15"));
        return false;
    }

    // 创建一个正则表达式对象，按照上述密码要求
    // 这个正则表达式解释：
    // ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
    QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*]{6,15}$");
    bool match = regExp.match(pass).hasMatch();
    if(!match){
        //提示字符非法
        AddTipErr(TipErr::TIP_PWD_ERR, tr("不能包含非法字符"));
        return false;;
    }

    DelTipErr(TipErr::TIP_PWD_ERR);

    return true;
}



bool Register_dialog::checkEmailValid()
{
    //验证邮箱的地址正则表达式
    auto email = ui->emial_edit->text();
    // 邮箱地址的正则表达式
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch(); // 执行正则表达式匹配
    if(!match){
        //提示邮箱不正确
        AddTipErr(TipErr::TIP_EMAIL_ERR, tr("邮箱地址不正确"));
        return false;
    }

    DelTipErr(TipErr::TIP_EMAIL_ERR);
    return true;
}

bool Register_dialog::checkVarifyValid()
{
    auto pass = ui->CAPTCHA_edit->text();
    if(pass.isEmpty()){
        AddTipErr(TipErr::TIP_VARIFY_ERR, tr("验证码不能为空"));
        return false;
    }

    DelTipErr(TipErr::TIP_VARIFY_ERR);
    return true;
}

Register_dialog::~Register_dialog()
{
    delete ui;
}

void Register_dialog::on_CAPTCHAget_btn_clicked()
{
    auto email = ui->emial_edit->text();
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch();
    if(match)
    {
        QJsonObject json;
        json["email"] = email;
        qDebug()<<json["email"];
        HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix+"/get_varifycode"),json,REQ_ID::ID_GET_VERIFY_CODE
                                            ,Modules::REGISTERMOD);

    }
    else{
        showTip(tr("邮箱格式不正确"),false);
    }
}
void Register_dialog::showTip(QString str,bool _b_ok)
{
    if(_b_ok)
    {
        ui->err_tip->setProperty("state","normal");
        ui->err_tip->setText(str);
        repolish(ui->err_tip);
    }
    else{
        ui->err_tip->setProperty("state","err");
        ui->err_tip->setText(str);
        repolish(ui->err_tip);
    }
}
void Register_dialog::slot_reg_mod_finish(REQ_ID id, QString res, ErrorCodes err){
    qDebug() << "=== 收到服务器响应 ===";
    qDebug() << "REQ_ID:" << static_cast<int>(id);
    qDebug() << "响应内容:" << res;
    qDebug() << "错误码:" << static_cast<int>(err);
    
    if(err!=ErrorCodes::Success)
    {
        showTip(tr("网络请求错误"),false);
        return;
    }
      QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    if(jsonDoc.isNull()){
        showTip(tr("json解析错误"),false);
        qDebug() << "JSON解析失败：文档为空";
        return;
    }
    if(!jsonDoc.isObject()){
        showTip(tr("json解析错误"),false);
        qDebug() << "JSON解析失败：不是对象";
        return;
    }

    qDebug() << "准备调用handler";
    _handler[id](jsonDoc.object());
    return;

}
void Register_dialog::initHttpHandlers(){
    _handler[REQ_ID::ID_GET_VERIFY_CODE] = [this](QJsonObject jsonobj){
        int error_code = jsonobj["error"].toInt();
        if(error_code != ErrorCodes::Success)
            {
            showTip(tr("参数错误"),false);
            return;
        }
        auto email = jsonobj["email"].toString();
        showTip(tr("验证码已发送到邮箱，注意查收"), true);
        qDebug()<< "email is " << email ;
    };
    _handler[REQ_ID::ID_REGISTER_USER] = [this](QJsonObject jsonobj){
        qDebug() << "=== 处理注册响应 ===";
        qDebug() << "完整JSON:" << jsonobj;
        
        int error_code = jsonobj["error"].toInt();
        qDebug() << "错误码:" << error_code;
        
        if(error_code != ErrorCodes::Success)
        {
            // 根据不同的错误码显示不同的提示
            switch(error_code) {
                case 1:
                    showTip(tr("用户名已存在"), false);
                    break;
                case 2:
                    showTip(tr("邮箱已存在"), false);
                    break;
                case 1003:
                    showTip(tr("验证码错误"), false);
                    break;
                case 1004:
                    showTip(tr("验证码已过期"), false);
                    break;
                default:
                    showTip(tr("注册失败，请稍后重试"), false);
                    break;
            }
            return;
        }
        auto email = jsonobj["email"].toString();
        emit Register_success();
        qDebug() << "注册成功！";
        showTip(tr("注册成功"), true);
        qDebug()<< "uid is " << jsonobj["uid"].toInt() << ", email is " << email;
        // TODO: 可以在这里跳转到登录界面或自动登录
    };
}

void Register_dialog::on_certain_btn_clicked()
{
    bool valid = checkUserValid();
    if(!valid){
        return;
    }

    valid = checkEmailValid();
    if(!valid){
        return;
    }

    valid = checkPassValid();
    if(!valid){
        return;
    }

    valid = checkVarifyValid();
    if(!valid){
        return;
    }

    // 客户端预哈希：SHA-256(明文密码)，防止明文密码在网络中传输
    auto sha256 = [](const QString& s) -> QString {
        return QString(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
    };

    QJsonObject json_obj;
    json_obj["user"] = ui->user_edit->text();
    json_obj["email"] = ui->emial_edit->text();
    json_obj["passwd"] = sha256(ui->pwd_edit->text());
    json_obj["confirm"] = sha256(ui->pwd_con_edit->text());
    json_obj["varifycode"] = ui->CAPTCHA_edit->text();
    
    qDebug() << "准备发送注册请求";
    qDebug() << "URL:" << (gate_url_prefix + "/user_register");
    qDebug() << "用户名:" << json_obj["user"].toString();
    qDebug() << "邮箱:" << json_obj["email"].toString();
    qDebug() << "验证码:" << json_obj["varifycode"].toString();
    
    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix+"/user_register"),
                                        json_obj, REQ_ID::ID_REGISTER_USER,Modules::REGISTERMOD);
    qDebug() << "注册请求已发送";
}


void Register_dialog::on_cancel_btn_clicked()
{
    emit switch_Login_Dialog();
}

