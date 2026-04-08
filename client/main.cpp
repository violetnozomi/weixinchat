#include "mainwindow.h"
#include "chatdialog.h"  // 添加聊天界面头文件
#include<QDebug>
#include <QApplication>
#include<QFile>
#include<QDir>
#include<QSettings>
#include"webrtcmgr.h"
// 调试开关：true=直接显示聊天界面，false=显示登录界面
#define DEBUG_CHAT_DIALOG false

int main(int argc, char *argv[])
{

    QApplication a(argc, argv);

    // 从资源文件加载样式表
    QFile qss(":/style/stylesheet.qss");
    
    // 获取当前应用程序的路径
    QString app_path = QCoreApplication::applicationDirPath();
    
    // 拼接配置文件名
    QString fileName = "config.ini";
    QString config_path = QDir::toNativeSeparators(app_path +
                                                   QDir::separator() + fileName);

    QSettings settings(config_path, QSettings::IniFormat);
    QString gate_host = settings.value("GateServer/host").toString();
    QString gate_port = settings.value("GateServer/port").toString();
    if (gate_host.isEmpty() || gate_port.isEmpty()) {
        qCritical() << "[Config] GateServer/host or GateServer/port is missing in" << config_path;
        return 1;
    }
    gate_url_prefix = "http://"+gate_host+":"+gate_port;
    // Read WebRTC ICE servers from config.
    QString stunServer = settings.value("WebRTC/stun_server",
                                        "stun:stun.l.google.com:19302").toString();
    QString turnServer = settings.value("WebRTC/turn_server", "").toString().trimmed();
    QString turnUsername = settings.value("WebRTC/turn_username", "").toString();
    QString turnPassword = settings.value("WebRTC/turn_password", "").toString();
    WebRTCMgr::GetInstance().SetStunServer(stunServer);
    WebRTCMgr::GetInstance().SetTurnServer(turnServer, turnUsername, turnPassword);
    if( qss.open(QFile::ReadOnly))
    {
        qDebug("open success");
        QString style = QLatin1String(qss.readAll());
        a.setStyleSheet(style);
        qss.close();
    }else{
        qDebug("Open failed");
    }
    WebRTCMgr::GetInstance().Init();
#if DEBUG_CHAT_DIALOG
    // 调试模式：直接显示聊天界面
    chatDialog chat;
    chat.show();
    return a.exec();
#else
    // 正常模式：显示登录界面

    MainWindow w;
    w.show();

    return a.exec();
#endif
}
