#ifndef FORGETDIALOG_H
#define FORGETDIALOG_H
#include<QString>
#include <QDialog>
#include"httpmgr.h"
namespace Ui {
class ForgetDialog;
}

class ForgetDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ForgetDialog(QWidget *parent = nullptr);
    ~ForgetDialog();
    void initHandlers();


private slots:
    void on_confirm_btn_clicked();

    void on_cancel_btn_clicked();
    
    void on_varify_btn_clicked();

private:
    Ui::ForgetDialog *ui;
    void AddTipErr(TipErr te ,QString tips);
    void DelTipErr(TipErr te);
    QMap<TipErr,QString> _tip_errs;
    void showTip(QString str ,bool b_ok);
    QMap<REQ_ID , std::function<void(const QJsonObject&)>> _handlers;
    bool checkUserValid();
    bool checkEmailValid();
    bool checkPassValid();
    bool checkVarifyValid();
    void slot_reset_mod_finish(REQ_ID id, QString res, ErrorCodes err);
signals:
    void switch_Login_dialog();
};

#endif // FORGETDIALOG_H
