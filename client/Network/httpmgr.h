#ifndef HTTPMGR_H
#define HTTPMGR_H
#include"Singleton.h"
#include <QJsonObject>
#include<QString>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include<QUrl>
class HttpMgr:public QObject,public Singleton<HttpMgr>,public std::enable_shared_from_this<HttpMgr>
{
    Q_OBJECT
public:~HttpMgr();
    void PostHttpReq(QUrl url, QJsonObject json, REQ_ID req_id, Modules mod);
private:
    HttpMgr();
    QNetworkAccessManager* _manger;
    friend class Singleton<HttpMgr>;

public slots:
        void slot_http_finish(REQ_ID req_id,QString res,ErrorCodes error_codes,Modules mod);
signals:
    void sig_http_finish(REQ_ID req_id,QString res,ErrorCodes err_codes,Modules mod);
    void sig_reg_mod_finish(REQ_ID req_id,QString res, ErrorCodes error_codes);
    void sig_reset_mod_finish(REQ_ID req_id,QString res, ErrorCodes error_codes);
    void sig_login_mod_finish(REQ_ID req_id,QString res, ErrorCodes error_codes);
    void sig_search_mod_finish(REQ_ID req_id,QString res, ErrorCodes error_codes);


};

#endif // HTTPMGR_H
