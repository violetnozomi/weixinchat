#include "forgetdialog.h"
#include "ui_forgetdialog.h"
#include <QCryptographicHash>

ForgetDialog::ForgetDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ForgetDialog)
{
    ui->setupUi(this);
    ui->err_tip->clear();
    
    // 设置密码输入框为密码模式
    ui->new_pwd_edit->setEchoMode(QLineEdit::Password);
    
    // 设置密码可见性切换图标
    ui->new_pwd_visble->setCursor(Qt::PointingHandCursor);
    ui->new_pwd_visble->SetState("unvisible","unvisible_hover","","visible",
                                 "visible_hover","");
    
    // 连接密码可见性切换事件
    connect(ui->new_pwd_visble, &ClickedLable::clicked, this, [this](){
        auto cur_state = ui->new_pwd_visble->GetCurState();
        if(cur_state == ClickLbState::Normal){
            ui->new_pwd_edit->setEchoMode(QLineEdit::Password);
        }else{
            ui->new_pwd_edit->setEchoMode(QLineEdit::Normal);
        }
    });
    
    // 设置"返回登录"链接样式和事件
    ui->return_login_label->setCursor(Qt::PointingHandCursor);
    ui->return_login_label->SetState("normal","hover","","selected","selected_hover","");
    connect(ui->return_login_label, &ClickedLable::clicked, this, [this](){
        emit switch_Login_dialog();
    });
    
    connect(ui->user_edit,&QLineEdit::editingFinished,this,[this](){
        checkUserValid();
    });

    connect(ui->email_edit, &QLineEdit::editingFinished, this, [this](){
        checkEmailValid();
    });

    connect(ui->new_pwd_edit, &QLineEdit::editingFinished, this, [this](){
        checkPassValid();
    });


    connect(ui->varify_edit, &QLineEdit::editingFinished, this, [this](){
        checkVarifyValid();
    });

    //连接reset相关信号和注册处理回调
    initHandlers();

    connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_reset_mod_finish, this,
            &ForgetDialog::slot_reset_mod_finish);
}
bool ForgetDialog::checkUserValid()
{
    if(ui->user_edit->text() == ""){
        AddTipErr(TipErr::TIP_USER_ERR, tr("用户名不能为空"));
        return false;
    }

    DelTipErr(TipErr::TIP_USER_ERR);
    return true;
}


bool ForgetDialog::checkPassValid()
{
    auto pass = ui->new_pwd_edit->text();

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

bool ForgetDialog::checkEmailValid()
{
    //验证邮箱的地址正则表达式
    auto email = ui->email_edit->text();
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

bool ForgetDialog::checkVarifyValid()
{
    auto pass = ui->varify_edit->text();
    if(pass.isEmpty()){
        AddTipErr(TipErr::TIP_VARIFY_ERR, tr("验证码不能为空"));
        return false;
    }

    DelTipErr(TipErr::TIP_VARIFY_ERR);
    return true;
}

void ForgetDialog::AddTipErr(TipErr te, QString tips)
{
    _tip_errs[te] = tips;
    showTip(tips, false);
}

void ForgetDialog::DelTipErr(TipErr te)
{
    _tip_errs.remove(te);
    if(_tip_errs.empty()){
        ui->err_tip->clear();
        return;
    }

    showTip(_tip_errs.first(), false);
}
void ForgetDialog::showTip(QString str, bool b_ok)
{
    if(b_ok){
        ui->err_tip->setProperty("state","normal");
    }else{
        ui->err_tip->setProperty("state","err");
    }

    ui->err_tip->setText(str);

    repolish(ui->err_tip);
}
void ForgetDialog::initHandlers()
{
    _handlers.insert(REQ_ID::ID_GET_VERIFY_CODE, [this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();
        if(error != ErrorCodes::Success){
            showTip(tr("参数错误"),false);
            return;
        }
        auto email = jsonObj["email"].toString();
        showTip(tr("验证码已发送到邮箱，注意查收"), true);
        qDebug()<< "email is " << email ;
    });

    //注册注册用户回包逻辑
    _handlers.insert(REQ_ID::ID_RESET_PWD, [this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();
        if(error != ErrorCodes::Success){
            // 根据不同的错误码显示不同的提示
            switch(error){
                case ErrorCodes::Error_Json:
                    showTip(tr("JSON格式错误"), false);
                    break;
                case ErrorCodes::Varify_error:
                    showTip(tr("验证码错误，请重新输入"), false);
                    break;
                case ErrorCodes::VarifyExpired:
                    showTip(tr("验证码已过期，请重新获取"), false);
                    break;
                case ErrorCodes::UserNotExist:
                    showTip(tr("用户不存在，请检查用户名"), false);
                    break;
                case ErrorCodes::EmailNotMatch:
                    showTip(tr("邮箱与用户名不匹配"), false);
                    break;
                case ErrorCodes::PasswdUpFailed:
                    showTip(tr("密码更新失败，请稍后重试"), false);
                    break;
                default:
                    showTip(tr("未知错误"), false);
                    break;
            }
            return;
        }
        
        // 重置成功
        auto email = jsonObj["email"].toString();
        showTip(tr("密码重置成功，点击返回登录"), true);
        qDebug()<< "密码重置成功，用户邮箱: " << email;
        qDebug()<< "user uuid is " <<  jsonObj["uuid"].toString();
    });
}
void ForgetDialog::slot_reset_mod_finish(REQ_ID id, QString res, ErrorCodes err){
    if(err != ErrorCodes::Success){
        showTip(tr("网络请求错误"),false);
        return;
    }

    // 解析 JSON 字符串,res需转化为QByteArray
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    //json解析错误
    if(jsonDoc.isNull()){
        showTip(tr("json解析错误"),false);
        return;
    }

    //json解析错误
    if(!jsonDoc.isObject()){
        showTip(tr("json解析错误"),false);
        return;
    }


    //调用对应的逻辑,根据id回调。
    _handlers[id](jsonDoc.object());

    return;
}
ForgetDialog::~ForgetDialog()
{
    delete ui;
}

void ForgetDialog::on_confirm_btn_clicked()
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
    QJsonObject jsonobj;
    // 客户端预哈希：SHA-256(明文密码)，防止明文密码在网络中传输
    auto sha256 = [](const QString& s) -> QString {
        return QString(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
    };

    jsonobj["user"] = ui->user_edit->text();
    jsonobj["email"] = ui->email_edit->text();
    jsonobj["varifycode"] = ui->varify_edit->text();  // 修复：改为varifycode，与服务端和注册功能保持一致
    jsonobj["new_pwd"] = sha256(ui->new_pwd_edit->text());
    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix+"/reset_pwd"),jsonobj,REQ_ID::ID_RESET_PWD,Modules::RESETPWDMOD);

}


void ForgetDialog::on_cancel_btn_clicked()
{
    emit switch_Login_dialog();
}

void ForgetDialog::on_varify_btn_clicked()
{
    auto email = ui->email_edit->text();
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch();
    if(match)
    {
        QJsonObject json;
        json["email"] = email;
        qDebug() << "重置密码-请求验证码: " << json["email"];
        HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix+"/get_varifycode"),
                                           json,
                                           REQ_ID::ID_GET_VERIFY_CODE,
                                           Modules::RESETPWDMOD);
    }
    else{
        showTip(tr("邮箱格式不正确"),false);
    }
}
