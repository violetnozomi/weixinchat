#include "tcpmgr.h"
#include "FileTransferMgr.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include"webrtcmgr.h"
TcpMgr::TcpMgr()
    : _pending(false)
    , _cur_msg_id(0)
    , _cur_msg_len(0)
    , _uid(0)
    , _state(ConnectState::Disconnected)
    , _is_logged_in(false)
    , _reconnect_attempt(0)
    , _reconnect_timer(new QTimer(this))
    , _heartbeat_timer(new QTimer(this))
    , _pong_timer(new QTimer(this))
{
    // Socket 信号
    connect(&_socket, &QTcpSocket::connected,      this, &TcpMgr::slot_connected);
    connect(&_socket, &QTcpSocket::disconnected,   this, &TcpMgr::slot_disconnected);
    connect(&_socket, &QTcpSocket::readyRead,       this, &TcpMgr::slot_read_data);
    connect(&_socket, &QTcpSocket::errorOccurred,  this, &TcpMgr::slot_error);

    // 重连定时器（单次触发）
    _reconnect_timer->setSingleShot(true);
    connect(_reconnect_timer, &QTimer::timeout, this, &TcpMgr::slot_do_reconnect);

    // 心跳定时器（循环）
    connect(_heartbeat_timer, &QTimer::timeout, this, &TcpMgr::slot_send_heartbeat);

    // pong 超时定时器（单次）
    _pong_timer->setSingleShot(true);
    connect(_pong_timer, &QTimer::timeout, this, [this]() {
        qDebug() << "[TCP] Pong timeout! Connection may be dead. Forcing disconnect.";
        _socket.abort();   // abort 会触发 disconnected 信号
    });

    initHandlers();
}

TcpMgr::~TcpMgr() {
    stopHeartbeat();
    _reconnect_timer->stop();
    _socket.close();
}

// ── 主动断连（登出时调用）────────────────────────────────────────────────────
void TcpMgr::Disconnect()
{
    _is_logged_in = false;       // 防止触发重连
    _state = ConnectState::Disconnected;
    stopHeartbeat();
    _reconnect_timer->stop();
    _reconnect_attempt = 0;
    _socket.abort();
    qDebug() << "[TCP] Explicitly disconnected (logout).";
}

// ── 重连调度 ─────────────────────────────────────────────────────────────────
void TcpMgr::scheduleReconnect()
{
    if (!_is_logged_in) return;  // 尚未登录成功（如首次认证失败），不重连

    _state = ConnectState::Reconnecting;
    _reconnect_attempt++;

    // 指数退退: 2^(attempt-1) 秒，封顶在 kMaxBackoffSec
    int delaySec = qMin(static_cast<int>(1 << _reconnect_attempt), kMaxBackoffSec);
    qDebug() << "[TCP] Scheduling reconnect attempt" << _reconnect_attempt
             << "in" << delaySec << "seconds";

    emit sig_reconnecting(_reconnect_attempt, delaySec);
    _reconnect_timer->start(delaySec * 1000);
}

// ── 心跳 ────────────────────────────────────────────────────────────────────
void TcpMgr::startHeartbeat()
{
    _heartbeat_timer->start(kHeartbeatMs);
    qDebug() << "[TCP] Heartbeat started (every" << kHeartbeatMs / 1000 << "s)";
}

void TcpMgr::stopHeartbeat()
{
    _heartbeat_timer->stop();
    _pong_timer->stop();
}

void TcpMgr::initHandlers() {
    // 认证响应
    _handlers[ID_CHAT_LOGIN_RSP] = [this](QJsonObject jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::Success) {
            QString msg = jsonObj["msg"].toString();
            qDebug() << "Chat server authentication failed:" << msg;
            _is_logged_in = false;
            emit sig_auth_failed(msg);
            _socket.close();
            return;
        }

        qDebug() << "Chat server authentication successful!";
        bool wasReconnect = (_reconnect_attempt > 0);
        _reconnect_attempt = 0;   // 重置退退计数器
        _is_logged_in      = true;
        _state             = ConnectState::Connected;
        startHeartbeat();

        if (wasReconnect) {
            qDebug() << "[TCP] Reconnected and re-authenticated.";
            emit sig_reconnected();
        } else {
            emit sig_auth_success();
        }
    };
    
    // 接收文本消息
    _handlers[ID_TEXT_CHAT_MSG] = [this](QJsonObject jsonObj) {
        int from_uid = jsonObj["from_uid"].toInt();
        QString content = jsonObj["content"].toString();
        qint64 timestamp = jsonObj["timestamp"].toVariant().toLongLong();
        
        qDebug() << "Received text message from" << from_uid << ":" << content;
        emit sig_text_msg_received(from_uid, content, timestamp);
    };
    
    // 发送消息响应
    _handlers[ID_TEXT_CHAT_MSG_RSP] = [this](QJsonObject jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::Success) {
            qDebug() << "Send message failed";
        } else {
            qDebug() << "Message sent successfully";
        }
    };

    // 添加好友请求的服务器回执（发送方收到）
    _handlers[ID_ADD_FRIEND_RSP] = [this](QJsonObject jsonObj) {
        int error = jsonObj["error"].toInt();
        QString msg = jsonObj["msg"].toString();
        qDebug() << "[TCP] AddFriend RSP: error=" << error << "msg=" << msg;
        emit sig_add_friend_rsp(error, msg);
    };

    // 收到别人发来的好友申请通知（接收方）
    _handlers[ID_NOTIFY_ADD_FRIEND] = [this](QJsonObject jsonObj) {
        int     from_uid  = jsonObj["from_uid"].toInt();
        QString from_name = jsonObj["from_name"].toString();
        QString message   = jsonObj["message"].toString();
        qDebug() << "[TCP] NOTIFY_ADD_FRIEND from uid=" << from_uid
                 << " name=" << from_name;
        emit sig_notify_add_friend(from_uid, from_name, message);
    };

    // 己方点击"接受"后，服务端反馈（已写库成功 + 对方信息）
    _handlers[ID_ACCEPT_FRIEND_RSP] = [this](QJsonObject jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::Success) {
            qDebug() << "[TCP] AcceptFriend RSP error=" << error;
            return;
        }
        int     friend_uid  = jsonObj["friend_uid"].toInt();
        QString friend_name = jsonObj["friend_name"].toString();
        qDebug() << "[TCP] AcceptFriend RSP: friend_uid=" << friend_uid
                 << " friend_name=" << friend_name;
        emit sig_accept_friend_rsp(friend_uid, friend_name);
    };

    // 对方同意了己方发出的好友申请（申请发起方收到）
    _handlers[ID_NOTIFY_ACCEPT_FRIEND] = [this](QJsonObject jsonObj) {
        int     from_uid  = jsonObj["from_uid"].toInt();
        QString from_name = jsonObj["from_name"].toString();
        qDebug() << "[TCP] NOTIFY_ACCEPT_FRIEND from uid=" << from_uid
                 << " name=" << from_name;
        emit sig_notify_accept_friend(from_uid, from_name);
    };

    // 登录时服务端推送好友列表
    _handlers[ID_FRIENDS_LIST_RSP] = [this](QJsonObject jsonObj) {
        QJsonArray arr = jsonObj["friends"].toArray();
        qDebug() << "[TCP] FRIENDS_LIST_RSP: received" << arr.size() << "friends";
        emit sig_friends_list(arr);
    };

    // 心跳 pong
    _handlers[ID_HEARTBEAT] = [this](QJsonObject /*jsonObj*/) {
        qDebug() << "[TCP] Pong received.";
        _pong_timer->stop();   // 收到噪声，停止超时定时器
    };

    // MinIO 文件传输（新协议，支持离线）
    _handlers[ID_FILE_MINIO_INIT_RSP] = [](QJsonObject json) {
        FileTransferMgr::GetInstance()->OnMinioInitRsp(json);
    };
    _handlers[ID_FILE_MINIO_COMMIT_RSP] = [](QJsonObject json) {
        FileTransferMgr::GetInstance()->OnMinioCommitRsp(json);
    };
    _handlers[ID_FILE_MINIO_NOTIFY] = [](QJsonObject json) {
        FileTransferMgr::GetInstance()->OnMinioNotify(json);
    };
    _handlers[ID_FILE_MINIO_REFRESH_RSP] = [](QJsonObject json) {
        FileTransferMgr::GetInstance()->OnMinioRefreshRsp(json);
    };

    // 删除好友响应
    _handlers[ID_DELETE_FRIEND_RSP] = [this](QJsonObject jsonObj) {
        int error     = jsonObj["error"].toInt();
        int friendUid = jsonObj["friend_uid"].toInt();
        qDebug() << "[TCP] DELETE_FRIEND_RSP: error=" << error << "uid=" << friendUid;
        emit sig_delete_friend_rsp(error, friendUid);
    };

    // 屏蔽好友响应
    _handlers[ID_BLOCK_FRIEND_RSP] = [this](QJsonObject jsonObj) {
        int error     = jsonObj["error"].toInt();
        int friendUid = jsonObj["friend_uid"].toInt();
        qDebug() << "[TCP] BLOCK_FRIEND_RSP: error=" << error << "uid=" << friendUid;
        emit sig_block_friend_rsp(error, friendUid);
    };

    // 资料修改响应
    _handlers[ID_UPDATE_PROFILE_RSP] = [this](QJsonObject jsonObj) {
        int error         = jsonObj["error"].toInt();
        QString name      = jsonObj["name"].toString();
        QString signature = jsonObj["signature"].toString();
        qDebug() << "[TCP] UPDATE_PROFILE_RSP: error=" << error << "name=" << name;
        emit sig_update_profile_rsp(error, name, signature);
    };
    _handlers[ID_WEBRTC_SIGNALING] = [this](QJsonObject jsonObj) {
        // 1. 优先检查服务器返回的错误码
        if (jsonObj.contains("error")) {
            int errCode = jsonObj["error"].toInt();
            if (errCode != 0) { // 0 通常代表 Success，非 0 代表出错了
                qDebug() << "[TCP] WebRTC 信令发送失败，错误码:" << errCode;

                // 如果是对方离线 (假设你的 ErrorCodes::UserOffline 是某个特定值)
                // 这里你可以直接调用挂断逻辑并提示 UI
                WebRTCMgr::GetInstance().HangUp();
                // emit sig_callFailed("对方不在线或通话请求失败");
                return;
            }
        }

        // 2. 提取基础路由信息
        int fromUid = jsonObj["from_uid"].toInt();
        QString type = jsonObj["type"].toString();

        // 兼容处理：服务器可能把 payload 作为 QJsonObject 转发，也可能作为 String 转发
        QJsonObject payload;
        if (jsonObj["payload"].isObject()) {
            payload = jsonObj["payload"].toObject();
        } else {
            QString payloadStr = jsonObj["payload"].toString();
            payload = QJsonDocument::fromJson(payloadStr.toUtf8()).object();
        }

        qDebug() << "[TCP] 收到有效 WebRTC 信令: type =" << type << ", fromUid =" << fromUid;

        // 3. 根据 type 路由给 WebRTCMgr
        if (type == "webrtc_offer") {
            QString sdp = payload["sdp"].toString();
            // 收到视频通话邀请
            WebRTCMgr::GetInstance().HandleRemoteOffer(fromUid, sdp);
        }
        else if (type == "webrtc_answer") {
            QString sdp = payload["sdp"].toString();
            // 对方同意了视频通话
            WebRTCMgr::GetInstance().HandleRemoteAnswer(fromUid, sdp);
        }
        else if (type == "webrtc_ice") {
            // 这里的键名必须与 WebRTCMgr::OnIceCandidate 发送时完全一致
            QString sdp_mid = payload["sdpMid"].toString(); // 注意：M 大写以符合规范
            int sdp_mline_index = payload["sdpMLineIndex"].toInt(); // 注意：L, I 大写
            QString candidate = payload["candidate"].toString();

            WebRTCMgr::GetInstance().HandleRemoteIceCandidate(fromUid, sdp_mid, sdp_mline_index, candidate);
        }
        else if (type == "webrtc_hangup") {
            qDebug() << "[TCP] 对方已挂断";
            WebRTCMgr::GetInstance().HandleRemoteHangUp(fromUid);
        }
        else {
            qDebug() << "[TCP] 未知的 WebRTC 信令类型:" << type;
        }
    };
}

void TcpMgr::slot_tcp_connect(QString host, QString port, QString token, int uid, QString user) {
    qDebug() << "[TCP] slot_tcp_connect called";

    _host  = host;
    _port  = port;
    _token = token;
    _uid   = uid;
    _user  = user;

    _is_logged_in      = false;
    _reconnect_attempt = 0;
    _state             = ConnectState::Connecting;

    qDebug() << "[TCP] Connecting to" << _host << ":" << _port.toUShort();
    _socket.connectToHost(_host, _port.toUShort());
}

void TcpMgr::slot_connected() {
    qDebug() << "[TCP] Successfully connected to chat server!";
    _state = ConnectState::Connecting;  // 进入认证阶段（认证成功后才算 Connected）
    emit sig_connect_success();

    // 发送认证请求（首次和重连后都走这条路）
    QJsonObject authObj;
    authObj["token"] = _token;
    authObj["uid"]   = _uid;
    authObj["user"]  = _user;
    SendMsg(ID_CHAT_LOGIN_REQ, authObj);
    qDebug() << "[TCP] Authentication request sent (uid=" << _uid << ")";
}

void TcpMgr::slot_disconnected() {
    qDebug() << "[TCP] Disconnected from chat server";
    stopHeartbeat();
    _data_buffer.clear();
    _pending = false;

    if (_is_logged_in) {
        // 意外断线 → 自动重连
        emit sig_connect_failed();
        scheduleReconnect();
    }
    // 如果是 Disconnect() 主动断开（_is_logged_in=false）则什么都不做
}

void TcpMgr::slot_error(QAbstractSocket::SocketError error) {
    // 连接失败 (ConnectError) 和断线 (RemoteHostClosedError) 不需要现在重连，
    // slot_disconnected 会随后触发。其他错误直接重连。
    qDebug() << "[TCP] Socket error:" << error << _socket.errorString();
    if (error != QAbstractSocket::RemoteHostClosedError) {
        stopHeartbeat();
        _data_buffer.clear();
        _pending = false;
        if (_is_logged_in && _state != ConnectState::Reconnecting) {
            emit sig_connect_failed();
            scheduleReconnect();
        } else if (!_is_logged_in && _state == ConnectState::Reconnecting) {
            // 重连期间连接失败 → 再试
            scheduleReconnect();
        }
    }
}

void TcpMgr::slot_do_reconnect()
{
    qDebug() << "[TCP] Attempting reconnect #" << _reconnect_attempt;
    _state = ConnectState::Connecting;
    _socket.abort();   // 确保先前状态清除
    _socket.connectToHost(_host, _port.toUShort());
}

void TcpMgr::slot_send_heartbeat()
{
    qDebug() << "[TCP] Sending heartbeat ping...";
    QJsonObject ping;
    ping["type"] = "ping";
    SendMsg(ID_HEARTBEAT, ping);
    // 启动 pong 超时定时器
    _pong_timer->start(kPongTimeoutMs);
}

void TcpMgr::slot_read_data() {
    // 读取所有可用数据追加到缓冲区
    _data_buffer.append(_socket.readAll());
    
    // 循环处理缓冲区中的消息（处理粘包）
    while (true) {
        // 如果还没有读取消息头
        if (!_pending) {
            // 消息头至少需要4字节 (id:2字节 + len:2字节)
            if (_data_buffer.size() < 4) {
                return;  // 数据不够，等待更多数据
            }
            
            // 解析消息头
            QDataStream stream(_data_buffer);
            stream.setByteOrder(QDataStream::BigEndian);
            stream >> _cur_msg_id >> _cur_msg_len;
            
            // 从缓冲区移除已读取的头部
            _data_buffer.remove(0, 4);
            _pending = true;
        }
        
        // 检查消息体是否完整
        if (_data_buffer.size() < _cur_msg_len) {
            return;  // 消息体不完整，等待更多数据
        }
        
        // 提取完整的消息体
        QByteArray msgData = _data_buffer.mid(0, _cur_msg_len);
        _data_buffer.remove(0, _cur_msg_len);
        
        // 处理消息
        handleMsg(static_cast<REQ_ID>(_cur_msg_id), _cur_msg_len, msgData);
        
        // 重置状态，准备读取下一条消息
        _pending = false;
        _cur_msg_id = 0;
        _cur_msg_len = 0;
    }
}

void TcpMgr::handleMsg(REQ_ID req_id, quint16 msg_len, QByteArray data) {
    qDebug() << "Received message: id=" << req_id << ", len=" << msg_len;
    
    // 解析JSON
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        qDebug() << "Invalid JSON in message";
        return;
    }
    
    QJsonObject jsonObj = jsonDoc.object();
    
    // 调用对应的处理器
    if (_handlers.contains(req_id)) {
        _handlers[req_id](jsonObj);
    } else {
        qDebug() << "No handler for message id:" << req_id;
    }
}

void TcpMgr::SendMsg(REQ_ID req_id, const QJsonObject& data) {
    // 序列化JSON
    QJsonDocument doc(data);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    
    // 构造消息：[id:2字节][len:2字节][json数据]
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    quint16 len = static_cast<quint16>(jsonData.size());
    quint16 msg_id = static_cast<quint16>(req_id);
    stream << msg_id << len;
    message.append(jsonData);
    
    // 发送
    qint64 written = _socket.write(message);
    if (written == -1) {
        qDebug() << "Failed to send message:" << _socket.errorString();
    } else {
        qDebug() << "Sent message: id=" << req_id << ", len=" << len;
    }
}
