#ifndef TCPMGR_H
#define TCPMGR_H

#include "Singleton.h"
#include "global.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

class TcpMgr : public QObject,
               public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
public:
    friend class Singleton<TcpMgr>;

    ~TcpMgr();
    void SendMsg(REQ_ID req_id, const QJsonObject &data);
    void Disconnect();

    int GetUid() const { return _uid; }
    QString GetUser() const { return _user; }
    QString GetToken() const { return _token; }
    QString GetHost() const { return _host; }
    QString GetPort() const { return _port; }

    enum class ConnectState {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting
    };
    ConnectState GetState() const { return _state; }

private:
    void initHandlers();
    void handleMsg(REQ_ID req_id, quint16 msg_len, QByteArray data);
    void scheduleReconnect();
    void startHeartbeat();
    void stopHeartbeat();

    QString _host;
    QString _port;
    QTcpSocket _socket;
    QByteArray _data_buffer;
    bool _pending;
    QMap<REQ_ID, std::function<void(QJsonObject)>> _handlers;
    quint16 _cur_msg_id;
    quint16 _cur_msg_len;

    QString _token;
    int _uid;
    QString _user;

    ConnectState _state;
    bool _is_logged_in;
    int _reconnect_attempt;
    QTimer *_reconnect_timer;

    QTimer *_heartbeat_timer;
    QTimer *_pong_timer;

    QSet<QString> _seen_msg_ids;
    QList<QString> _seen_msg_ids_fifo;
    static constexpr int kMaxSeenMsgIds = 500;
    static constexpr int kMaxBackoffSec = 30;
    static constexpr int kHeartbeatMs = 30000;
    static constexpr int kPongTimeoutMs = 10000;

    TcpMgr();

signals:
    void sig_connect_success();
    void sig_connect_failed();
    void sig_auth_success();
    void sig_auth_failed(QString error);
    void sig_reconnecting(int attempt, int nextDelaySec);
    void sig_reconnected();

    void sig_text_msg_received(int from_uid, QString content, qint64 timestamp);
    void sig_text_msg_self_echo(int to_uid, QString content, qint64 timestamp);
    void sig_ai_text_msg_received(int from_uid, QString content, qint64 timestamp);
    void sig_stream_start(int from_uid, QString stream_id, qint64 timestamp);
    void sig_stream_chunk(int from_uid, QString stream_id, QString piece);
    void sig_stream_end(int from_uid, QString stream_id);
    void sig_add_friend_rsp(int error, QString msg);
    void sig_notify_add_friend(int from_uid, QString from_name, QString message);
    void sig_accept_friend_rsp(int friend_uid, QString friend_name);
    void sig_notify_accept_friend(int from_uid, QString from_name);
    void sig_friends_list(QJsonArray friends);
    void sig_delete_friend_rsp(int error, int friendUid);
    void sig_block_friend_rsp(int error, int friendUid);
    void sig_update_profile_rsp(int error, QString name, QString signature);

    void sig_group_created(int error, QString groupId, QString groupName,
                           int memberCount, QString msg);
    void sig_group_info_received(int error, QString groupId, QString groupName,
                                 QString avatarPath, QString notice,
                                 int ownerUid, int memberCount, QString msg);
    void sig_group_members_received(int error, QString groupId, QJsonArray members, QString msg);
    void sig_group_text_msg_received(QString groupId, int senderUid,
                                     QString content, qint64 timestamp, QString msgId);
    void sig_group_text_msg_rsp(int error, QString groupId, QString msgId,
                                qint64 timestamp, QString msg);
    void sig_group_sync_received(int error, QString groupId, QString latestMsgId,
                                 QJsonArray messages, QString msg);

public slots:
    void slot_tcp_connect(QString host, QString port, QString token, int uid, QString user);

private slots:
    void slot_connected();
    void slot_disconnected();
    void slot_read_data();
    void slot_error(QAbstractSocket::SocketError error);
    void slot_do_reconnect();
    void slot_send_heartbeat();
};

#endif // TCPMGR_H
