#ifndef REGISTER_DIALOG_H
#define REGISTER_DIALOG_H
#include"global.h"
#include <QDialog>
#include<qstring.h>
#include<qmap.h>
#include <QLineEdit>
namespace Ui {
class Register_dialog;
}

class Register_dialog : public QDialog
{
    Q_OBJECT

public:
    explicit Register_dialog(QWidget *parent = nullptr);
    ~Register_dialog();

private slots:
    void on_CAPTCHAget_btn_clicked();
    void slot_reg_mod_finish(REQ_ID id, QString res, ErrorCodes err);
    bool checkUserValid();
    bool checkPassValid();
    bool checkEmailValid();
    bool checkConfirmValid();
    bool checkVarifyValid();
    void on_certain_btn_clicked();

    void on_cancel_btn_clicked();

private:
    void showTip(QString str,bool _b_ok);
    void AddTipErr(TipErr te, QString tips);
    void DelTipErr(TipErr te);
    void initHttpHandlers();
    QMap<TipErr,QString> _tip_errs;
    Ui::Register_dialog *ui;
    std::unordered_map<REQ_ID ,std::function<void(const QJsonObject&)>> _handler;
signals:
    void Register_success();
    void switch_Login_Dialog();
};

#endif // REGISTER_DIALOG_H
