#include "tcpmgr.h"
#include "FileTransferMgr.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include"webrtcmgr.h"

namespace {

QString JsonIdToString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(static_cast<qulonglong>(value.toDouble()));
    }
    return QString();
}

bool IsValidUtf8(const QByteArray &data)
{
    const auto *bytes =
        reinterpret_cast<const unsigned char *>(data.constData());
    const int size = data.size();
    int i = 0;
    while (i < size) {
        const unsigned char ch = bytes[i];
        if (ch <= 0x7f) {
            ++i;
            continue;
        }

        int needed = 0;
        if ((ch & 0xe0) == 0xc0) {
            if (ch < 0xc2) {
                return false;
            }
            needed = 1;
        } else if ((ch & 0xf0) == 0xe0) {
            needed = 2;
        } else if ((ch & 0xf8) == 0xf0) {
            if (ch > 0xf4) {
                return false;
            }
            needed = 3;
        } else {
            return false;
        }

        if (i + needed >= size) {
            return false;
        }

        for (int j = 1; j <= needed; ++j) {
            if ((bytes[i + j] & 0xc0) != 0x80) {
                return false;
            }
        }

        if (needed == 2) {
            const unsigned char b1 = bytes[i + 1];
            if (ch == 0xe0 && b1 < 0xa0) {
                return false;
            }
            if (ch == 0xed && b1 >= 0xa0) {
                return false;
            }
        } else if (needed == 3) {
            const unsigned char b1 = bytes[i + 1];
            if (ch == 0xf0 && b1 < 0x90) {
                return false;
            }
            if (ch == 0xf4 && b1 > 0x8f) {
                return false;
            }
        }

        i += needed + 1;
    }
    return true;
}

} // namespace

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
    _socket.abort();  // abort 比 close 快，不会阻塞等待 pending write
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

    // 指数退避: 2^attempt 秒，封顶在 kMaxBackoffSec
    if (_reconnect_attempt > 30) _reconnect_attempt = 30;  // 防止移位溢出
    int delaySec = qMin(1 << _reconnect_attempt, kMaxBackoffSec);
    qDebug() << "[TCP] Scheduling reconnect attempt" << _reconnect_attempt
             << "in" << delaySec << "seconds";

    emit sig_reconnecting(_reconnect_attempt, delaySec);
    _reconnect_timer->start(delaySec * 1000);
}

// ── 心跳 ────────────────────────────────────────────────────────────────────
void TcpMgr::startHeartbeat()
{
    _heartbeat_timer->start(kHeartbeatMs);
}

void TcpMgr::stopHeartbeat()
{
    _heartbeat_timer->stop();
    _pong_timer->stop();
}

void TcpMgr::initHandlers() {
    const auto dispatchChatLikePayload = [this](const QJsonObject &jsonObj,
                                                const char *sourceTag,
                                                const std::function<void(int, const QString&, qint64)> &plainDispatch) {
        int from_uid = jsonObj["from_uid"].toInt();
        if (from_uid <= 0) {
            qWarning() << "[TCP]" << sourceTag << "invalid from_uid";
            return;
        }

        QString msgId = jsonObj["msg_id"].toString();
        if (!msgId.isEmpty()) {
            if (_seen_msg_ids.contains(msgId)) {
                qDebug() << "[TCP]" << sourceTag << "duplicate msg_id=" << msgId << ", discarded";
                return;
            }
            _seen_msg_ids.insert(msgId);
            _seen_msg_ids_fifo.append(msgId);
            if (_seen_msg_ids_fifo.size() > kMaxSeenMsgIds) {
                _seen_msg_ids.remove(_seen_msg_ids_fifo.takeFirst());
            }
        }

        QString content = jsonObj["content"].toString();
        qint64 timestamp = jsonObj["timestamp"].toVariant().toLongLong();
        QString streamStatus = jsonObj["stream_status"].toString();
        if (!streamStatus.isEmpty()) {
            QString streamId = jsonObj["stream_id"].toString();
            qDebug() << "[TCP]" << sourceTag << "stream frame"
                     << "status=" << streamStatus << "stream_id=" << streamId;
            if (streamStatus == "start") {
                emit sig_stream_start(from_uid, streamId, timestamp);
            } else if (streamStatus == "streaming") {
                emit sig_stream_chunk(from_uid, streamId, content);
            } else if (streamStatus == "end") {
                emit sig_stream_end(from_uid, streamId);
            }
            return;
        }

        qDebug() << "[TCP]" << sourceTag << "plain message from=" << from_uid
                 << "len=" << content.size();
        plainDispatch(from_uid, content, timestamp);
    };
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

        bool wasReconnect = (_reconnect_attempt > 0);
        _reconnect_attempt = 0;
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
    
    // 文本消息/流式回复
    _handlers[ID_TEXT_CHAT_MSG] = [dispatchChatLikePayload, this](QJsonObject jsonObj) {
        dispatchChatLikePayload(
            jsonObj,
            "text",
            [this](int from_uid, const QString &content, qint64 timestamp) {
                emit sig_text_msg_received(from_uid, content, timestamp);
            });
    };

    // 发送消息响应
    _handlers[ID_AI_CHAT_MSG] = [dispatchChatLikePayload, this](QJsonObject jsonObj) {
        dispatchChatLikePayload(
            jsonObj,
            "ai",
            [this](int from_uid, const QString &content, qint64 timestamp) {
                emit sig_ai_text_msg_received(from_uid, content, timestamp);
            });
    };

    _handlers[ID_TEXT_CHAT_MSG_RSP] = [this](QJsonObject jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::Success) {
            qWarning() << "[TCP] Send message failed: error=" << error;
            return;
        }

        if (jsonObj["echo_self"].toBool()) {
            const int to_uid = jsonObj["to_uid"].toInt();
            const QString content = jsonObj["content"].toString();
            const qint64 timestamp = jsonObj["timestamp"].toVariant().toLongLong();
            qDebug() << "[TCP] self echo message to=" << to_uid
                     << "len=" << content.size();
            emit sig_text_msg_self_echo(to_uid, content, timestamp);
        }
    };

    // 添加好友请求的服务器回执（发送方收到）
    _handlers[ID_ADD_FRIEND_RSP] = [this](QJsonObject jsonObj) {
        int error = jsonObj["error"].toInt();
        QString msg = jsonObj["msg"].toString();
        emit sig_add_friend_rsp(error, msg);
    };

    // 收到别人发来的好友申请通知（接收方）
    _handlers[ID_NOTIFY_ADD_FRIEND] = [this](QJsonObject jsonObj) {
        int     from_uid  = jsonObj["from_uid"].toInt();
        if (from_uid <= 0) {
            qWarning() << "[TCP] NOTIFY_ADD_FRIEND: invalid from_uid, discarded";
            return;
        }
        QString from_name = jsonObj["from_name"].toString();
        QString message   = jsonObj["message"].toString();
        emit sig_notify_add_friend(from_uid, from_name, message);
    };

    // 己方点击"接受"后，服务端反馈（已写库成功 + 对方信息）
    _handlers[ID_ACCEPT_FRIEND_RSP] = [this](QJsonObject jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::Success) {
            qWarning() << "[TCP] AcceptFriend RSP error=" << error;
            return;
        }
        int     friend_uid  = jsonObj["friend_uid"].toInt();
        QString friend_name = jsonObj["friend_name"].toString();
        emit sig_accept_friend_rsp(friend_uid, friend_name);
    };

    // 对方同意了己方发出的好友申请（申请发起方收到）
    _handlers[ID_NOTIFY_ACCEPT_FRIEND] = [this](QJsonObject jsonObj) {
        int     from_uid  = jsonObj["from_uid"].toInt();
        QString from_name = jsonObj["from_name"].toString();
        emit sig_notify_accept_friend(from_uid, from_name);
    };

    // 登录时服务端推送好友列表
    _handlers[ID_FRIENDS_LIST_RSP] = [this](QJsonObject jsonObj) {
        QJsonArray arr = jsonObj["friends"].toArray();
        emit sig_friends_list(arr);
    };

    // 心跳 pong
    _handlers[ID_HEARTBEAT] = [this](QJsonObject /*jsonObj*/) {
        _pong_timer->stop();   // 收到 pong，停止超时定时器
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
    _handlers[ID_FILE_MINIO_MP_INIT_RSP] = [](QJsonObject json) {
        FileTransferMgr::GetInstance()->OnMpInitRsp(json);
    };
    _handlers[ID_FILE_MINIO_MP_COMPLETE_RSP] = [](QJsonObject json) {
        FileTransferMgr::GetInstance()->OnMpCompleteRsp(json);
    };
    _handlers[ID_FILE_MINIO_MP_RESUME_RSP] = [](QJsonObject json) {
        FileTransferMgr::GetInstance()->OnMpResumeRsp(json);
    };

    // 删除好友响应
    _handlers[ID_DELETE_FRIEND_RSP] = [this](QJsonObject jsonObj) {
        int error     = jsonObj["error"].toInt();
        int friendUid = jsonObj["friend_uid"].toInt();
        emit sig_delete_friend_rsp(error, friendUid);
    };

    // 屏蔽好友响应
    _handlers[ID_BLOCK_FRIEND_RSP] = [this](QJsonObject jsonObj) {
        int error     = jsonObj["error"].toInt();
        int friendUid = jsonObj["friend_uid"].toInt();
        emit sig_block_friend_rsp(error, friendUid);
    };

    // 资料修改响应
    _handlers[ID_UPDATE_PROFILE_RSP] = [this](QJsonObject jsonObj) {
        int error         = jsonObj["error"].toInt();
        QString name      = jsonObj["name"].toString();
        QString signature = jsonObj["signature"].toString();
        emit sig_update_profile_rsp(error, name, signature);
    };

    _handlers[ID_CREATE_GROUP_RSP] = [this](QJsonObject jsonObj) {
        emit sig_group_created(
            jsonObj["error"].toInt(),
            JsonIdToString(jsonObj["group_id"]),
            jsonObj["group_name"].toString(),
            jsonObj["member_count"].toInt(),
            jsonObj["msg"].toString());
    };

    _handlers[ID_GROUP_INFO_RSP] = [this](QJsonObject jsonObj) {
        emit sig_group_info_received(
            jsonObj["error"].toInt(),
            JsonIdToString(jsonObj["group_id"]),
            jsonObj["group_name"].toString(),
            jsonObj["avatar_path"].toString(),
            jsonObj["notice"].toString(),
            jsonObj["owner_uid"].toInt(),
            jsonObj["member_count"].toInt(),
            jsonObj["msg"].toString());
    };

    _handlers[ID_GROUP_MEMBER_LIST_RSP] = [this](QJsonObject jsonObj) {
        emit sig_group_members_received(
            jsonObj["error"].toInt(),
            JsonIdToString(jsonObj["group_id"]),
            jsonObj["members"].toArray(),
            jsonObj["msg"].toString());
    };

    _handlers[ID_GROUP_TEXT_MSG] = [this](QJsonObject jsonObj) {
        const QString groupId = JsonIdToString(jsonObj["group_id"]);
        const int senderUid = jsonObj["sender_uid"].toInt();
        if (groupId.isEmpty() || senderUid <= 0) {
            qWarning() << "[TCP] Invalid group text payload";
            return;
        }

        emit sig_group_text_msg_received(
            groupId,
            senderUid,
            jsonObj["content"].toString(),
            jsonObj["timestamp"].toVariant().toLongLong(),
            JsonIdToString(jsonObj["msg_id"]));
    };

    _handlers[ID_GROUP_TEXT_MSG_RSP] = [this](QJsonObject jsonObj) {
        emit sig_group_text_msg_rsp(
            jsonObj["error"].toInt(),
            JsonIdToString(jsonObj["group_id"]),
            JsonIdToString(jsonObj["msg_id"]),
            jsonObj["timestamp"].toVariant().toLongLong(),
            jsonObj["msg"].toString());
    };

    _handlers[ID_GROUP_SYNC_RSP] = [this](QJsonObject jsonObj) {
        emit sig_group_sync_received(
            jsonObj["error"].toInt(),
            JsonIdToString(jsonObj["group_id"]),
            JsonIdToString(jsonObj["latest_msg_id"]),
            jsonObj["messages"].toArray(),
            jsonObj["msg"].toString());
    };
    _handlers[ID_WEBRTC_SIGNALING] = [this](QJsonObject jsonObj) {
        // 1. 优先检查服务器返回的错误码
        if (jsonObj.contains("error")) {
            int errCode = jsonObj["error"].toInt();
            if (errCode != 0) { // 0 通常代表 Success，非 0 代表出错了
        qDebug() << "[TCP] WebRTC 信令发送失败，错误码:" << errCode;
                WebRTCMgr::GetInstance().HangUp();
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
            WebRTCMgr::GetInstance().HandleRemoteOffer(fromUid, sdp);
        }
        else if (type == "webrtc_answer") {
            QString sdp = payload["sdp"].toString();
            WebRTCMgr::GetInstance().HandleRemoteAnswer(fromUid, sdp);
        }
        else if (type == "webrtc_ice") {
            QString sdp_mid = payload["sdpMid"].toString();
            int sdp_mline_index = payload["sdpMLineIndex"].toInt();
            QString candidate = payload["candidate"].toString();
            WebRTCMgr::GetInstance().HandleRemoteIceCandidate(fromUid, sdp_mid, sdp_mline_index, candidate);
        }
        else if (type == "webrtc_hangup") {
            qDebug() << "[TCP] 对方已挂断";
            WebRTCMgr::GetInstance().HandleRemoteHangUp(fromUid);
        }
    };
}

void TcpMgr::slot_tcp_connect(QString host, QString port, QString token, int uid, QString user) {
    _host  = host;
    _port  = port;
    _token = token;
    _uid   = uid;
    _user  = user;

    _is_logged_in      = false;
    _reconnect_attempt = 0;
    _state             = ConnectState::Connecting;

    _socket.connectToHost(_host, _port.toUShort());
}

void TcpMgr::slot_connected() {
    _state = ConnectState::Connecting;  // 进入认证阶段（认证成功后才算 Connected）
    emit sig_connect_success();

    // 发送认证请求（首次和重连后都走这条路）
    QJsonObject authObj;
    authObj["token"] = _token;
    authObj["uid"]   = _uid;
    authObj["user"]  = _user;
    SendMsg(ID_CHAT_LOGIN_REQ, authObj);
}

void TcpMgr::slot_disconnected() {
    qDebug() << "[TCP] disconnected state=" << static_cast<int>(_state)
             << "logged_in=" << _is_logged_in;
    stopHeartbeat();
    _data_buffer.clear();
    _pending = false;

    if (_is_logged_in) {
        // 意外断线 → 自动重连
        emit sig_connect_failed();
        scheduleReconnect();
    }
}

void TcpMgr::slot_error(QAbstractSocket::SocketError error) {
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
    _state = ConnectState::Connecting;
    _socket.abort();
    _socket.connectToHost(_host, _port.toUShort());
}

void TcpMgr::slot_send_heartbeat()
{
    QJsonObject ping;
    ping["type"] = "ping";
    SendMsg(ID_HEARTBEAT, ping);
    _pong_timer->stop();  // 先停止，避免重叠
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

            // 防止恶意/异常的超大包耗尽内存
            static constexpr quint16 kMaxMsgLen = 60000;
            if (_cur_msg_len > kMaxMsgLen) {
                qWarning() << "[TCP] msg_len" << _cur_msg_len << "exceeds limit, disconnecting";
                _data_buffer.clear();
                _pending = false;
                _socket.abort();
                return;
            }

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
    Q_UNUSED(msg_len)

    if (!IsValidUtf8(data)) {
        qWarning() << "[TCP] Invalid UTF-8 payload in message id=" << req_id
                   << "size=" << data.size()
                   << "hex_prefix=" << data.left(48).toHex(' ');
        return;
    }

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        qWarning() << "[TCP] Invalid JSON in message id=" << req_id
                   << "error=" << parseError.errorString()
                   << "offset=" << parseError.offset
                   << "payload_prefix=" << QString::fromUtf8(data.left(160));
        return;
    }
    
    QJsonObject jsonObj = jsonDoc.object();
    
    if (_handlers.contains(req_id)) {
        _handlers[req_id](jsonObj);
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
        qWarning() << "[TCP] Failed to send message id=" << req_id << ":" << _socket.errorString();
    }
}
