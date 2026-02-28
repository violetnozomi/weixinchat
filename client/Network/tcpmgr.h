#ifndef TCPMGR_H
#define TCPMGR_H
#include"Singleton.h"
#include"global.h"
#include<QString>
#include<QJsonArray>
#include<QTcpSocket>
#include<QTimer>
#include<QMap>
#include<QJsonDocument>
class TcpMgr:public QObject,public Singleton<TcpMgr>,public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
public:
    friend class Singleton<TcpMgr>;
    ~TcpMgr();
    void SendMsg(REQ_ID req_id, const QJsonObject& data);

    // 主动断连（登出时调用，防止触发重连）
    void Disconnect();

    // 获取用户信息
    int     GetUid()   const { return _uid;   }
    QString GetUser()  const { return _user;  }
    QString GetToken() const { return _token; }
    QString GetHost()  const { return _host;  }
    QString GetPort()  const { return _port;  }

    // 连接状态
    enum class ConnectState { Disconnected, Connecting, Connected, Reconnecting };
    ConnectState GetState() const { return _state; }

private:
    void initHandlers();
    void handleMsg(REQ_ID req_id, quint16 msg_len, QByteArray data);
    void scheduleReconnect();   // 计算退退延迟并启动重连定时器
    void startHeartbeat();      // 开始心跳计时
    void stopHeartbeat();       // 停止心跳计时

    // 网络
    QString _host;
    QString _port;
    QTcpSocket _socket;
    QByteArray _data_buffer;
    bool _pending;
    QMap<REQ_ID,std::function<void(QJsonObject)>> _handlers;
    quint16 _cur_msg_id;
    quint16 _cur_msg_len;

    // 用户信息
    QString _token;
    int _uid;
    QString _user;

    // 重连状态
    ConnectState _state;
    bool         _is_logged_in;       // 曾经登录成功过（断线后才重连）
    int          _reconnect_attempt;  // 当前重连次数
    QTimer      *_reconnect_timer;    // 重连延迟定时器（单次）

    // 心跳
    QTimer      *_heartbeat_timer;    // 每 30s 发一次 ping
    QTimer      *_pong_timer;         // ping 后 10s 未收到 pong → 主动断线

    // 最大重连间隔（秒）
    static constexpr int kMaxBackoffSec = 30;
    // 心跳间隔（毫秒）
    static constexpr int kHeartbeatMs  = 30000;
    // pong 超时（毫秒）
    static constexpr int kPongTimeoutMs = 10000;

    TcpMgr();
signals:
    void sig_connect_success();
    void sig_connect_failed();
    void sig_auth_success();
    void sig_auth_failed(QString error);
    // 重连相关信号
    void sig_reconnecting(int attempt, int nextDelaySec);  // 正在重连中
    void sig_reconnected();                                // 重连并重认证成功
    // 业务消息
    void sig_text_msg_received(int from_uid, QString content, qint64 timestamp);
    void sig_add_friend_rsp(int error, QString msg);               // 发出好友申请后的服务器回执
    void sig_notify_add_friend(int from_uid, QString from_name, QString message); // 收到别人的好友申请
    // 同意好友申请相关信号
    void sig_accept_friend_rsp(int friend_uid, QString friend_name);  // 己方同意后，服务端确认+好友信息
    void sig_notify_accept_friend(int from_uid, QString from_name);   // 对方同意了己方的好友申请
    void sig_friends_list(QJsonArray friends);  // 登录时服务端推送好友列表
    void sig_delete_friend_rsp(int error, int friendUid);  // 删除好友结果
    void sig_block_friend_rsp (int error, int friendUid);  // 屏蔽好友结果
    void sig_update_profile_rsp(int error, QString name, QString signature); // 资料修改结果
    
public slots:
    void slot_tcp_connect(QString host, QString port, QString token, int uid, QString user);

private slots:
    void slot_connected();
    void slot_disconnected();
    void slot_read_data();
    void slot_error(QAbstractSocket::SocketError error);
    void slot_do_reconnect();   // 重连定时器触发
    void slot_send_heartbeat(); // 心跳定时器触发
};

#endif // TCPMGR_H
