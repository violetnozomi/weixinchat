#include "httpmgr.h"
#include <QNetworkReply>
#include <QDebug>
HttpMgr::HttpMgr() {

    qDebug() << "Debug: 进入 HttpMgr 构造函数"; // <--- 加这行

    // 【关键】必须先分配内存
    _manger = new QNetworkAccessManager(this);

    connect(this, &HttpMgr::sig_http_finish, this, &HttpMgr::slot_http_finish);

    qDebug() << "Debug: HttpMgr 构造完毕"; // <--- 加这行

}
void HttpMgr::PostHttpReq(QUrl url, QJsonObject json, REQ_ID req_id, Modules mod)
{
    qDebug() << "[HTTP] PostHttpReq called";
    qDebug() << "[HTTP]   URL:" << url.toString();
    qDebug() << "[HTTP]   Request ID:" << (int)req_id;
    qDebug() << "[HTTP]   Module:" << (int)mod;
    qDebug() << "[HTTP]   JSON:" << json;
    
    QByteArray data = QJsonDocument(json).toJson();
    QNetworkRequest Request(url);
    Request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    Request.setHeader(QNetworkRequest::ContentLengthHeader,QByteArray::number(data.length()));
    auto self = shared_from_this();
    QNetworkReply* Reply = _manger->post(Request,data);
    qDebug() << "[HTTP] Request sent, waiting for response...";
    qDebug()<<Reply;
QObject::connect(Reply, &QNetworkReply::finished, [self, Reply, req_id, mod](){
    qDebug() << "[HTTP] Response received";
    
    if(Reply->error() != QNetworkReply::NoError) {
        qDebug() << "[HTTP] Network error:" << Reply->error();
        qDebug() << "[HTTP] Error string:" << Reply->errorString();


        emit self->sig_http_finish(req_id, "", ErrorCodes::RPCFailed, mod);
        Reply->deleteLater();
        return;
    }
    QString res = Reply->readAll();
    qDebug() << "[HTTP] Response body:" << res;
    emit self->sig_http_finish(req_id , res , ErrorCodes::Success, mod);
    Reply->deleteLater();
    return;

}
                     );
}


void HttpMgr::slot_http_finish(REQ_ID req_id,QString res,ErrorCodes error_codes,Modules mod){
    qDebug() << "[HTTP] slot_http_finish called";
    qDebug() << "[HTTP]   Request ID:" << (int)req_id;
    qDebug() << "[HTTP]   Module:" << (int)mod;
    qDebug() << "[HTTP]   Error Code:" << (int)error_codes;
    qDebug() << "[HTTP]   Response:" << res;
    
    if(mod == Modules::REGISTERMOD)
    {
        qDebug() << "[HTTP] Emitting sig_reg_mod_finish";
        emit sig_reg_mod_finish(req_id,res,error_codes);
    }
    if(mod == Modules::RESETPWDMOD)
    {
        qDebug() << "[HTTP] Emitting sig_reset_mod_finish";
        emit sig_reset_mod_finish(req_id,res,error_codes);
    }
    if(mod == Modules::USERLOGIN){
        qDebug() << "[HTTP] Emitting sig_login_mod_finish";
        emit sig_login_mod_finish(req_id,res,error_codes);
    }
    if(mod == Modules::SEARCHMOD){
        qDebug() << "[HTTP] Emitting sig_search_mod_finish";
        emit sig_search_mod_finish(req_id,res,error_codes);
    }
}
HttpMgr::~HttpMgr()
{

}
