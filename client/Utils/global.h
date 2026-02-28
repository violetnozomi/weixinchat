#ifndef GLOBAL_H
#define GLOBAL_H
#include <QWidget>
#include <functional>
#include<QObject>
#include<memory.h>
#include<mutex>
#include<QRegularExpressionMatch>
#include "QStyle"
#include<QString>
#include<unordered_map>
extern std::function<void(QWidget*)> repolish;
extern QString gate_url_prefix;


enum REQ_ID{
    // HTTP请求消息 (1000-1999)
    ID_GET_VERIFY_CODE = 1001,
    ID_REGISTER_USER = 1002,
    ID_RESET_PWD = 1003,
    ID_LOGIN_USER = 1004,
    ID_SEARCH_USER = 1005,         // HTTP 搜索用户（发�?GateServer�?

    // TCP聊天消息 (2000-2999)
    ID_CHAT_LOGIN_REQ = 2001,      // 聊天服务器认证请�?
    ID_CHAT_LOGIN_RSP = 2002,      // 聊天服务器认证响�?
    ID_TEXT_CHAT_MSG = 2003,       // 发送文本消�?
    ID_TEXT_CHAT_MSG_RSP = 2004,   // 文本消息响应
    ID_SEARCH_USER_REQ = 2005,     // 搜索用户请求（TCP，暂未使用）
    ID_SEARCH_USER_RSP = 2006,     // 搜索用户响应（TCP，暂未使用）
    ID_ADD_FRIEND_REQ = 2007,      // 添加好友请求
    ID_ADD_FRIEND_RSP = 2008,      // 添加好友响应
    ID_NOTIFY_ADD_FRIEND = 2009,   // 好友申请通知（被动接收）
    ID_ACCEPT_FRIEND_REQ = 2010,   // 同意好友申请请求
    ID_ACCEPT_FRIEND_RSP = 2011,   // 同意好友申请响应
    ID_NOTIFY_ACCEPT_FRIEND = 2012,// 好友已同意通知（申请发起方收到�?
    ID_FRIENDS_LIST_RSP = 2013,    // 登录时服务端推送好友列�?

    // 文件传输 (2014-2022)

    // 好友管理扩展 (2023-2026)
    ID_DELETE_FRIEND_REQ     = 2023,  // client→server: 删除好友
    ID_DELETE_FRIEND_RSP     = 2024,  // server→client: 删除结果
    ID_BLOCK_FRIEND_REQ      = 2025,  // client→server: 屏蔽好友
    ID_BLOCK_FRIEND_RSP      = 2026,  // server→client: 屏蔽结果

    // 用户资料修改 (2027-2028)
    ID_UPDATE_PROFILE_REQ    = 2027,  // client→server: 修改昵称/签名/头像
    ID_UPDATE_PROFILE_RSP    = 2028,  // server→client: 修改结果

    // ── MinIO 对象存储文件传输 (2030-2036) ─────────────────────────────────
    ID_FILE_MINIO_INIT_REQ     = 2030,  // client→server: 申请上传预签�?PUT URL
    ID_FILE_MINIO_INIT_RSP     = 2031,  // server→client: { file_key, upload_url }
    ID_FILE_MINIO_COMMIT       = 2032,  // client→server: 文件已上传到MinIO，提交记�?
    ID_FILE_MINIO_COMMIT_RSP   = 2033,  // server→client: { error }
    ID_FILE_MINIO_NOTIFY       = 2034,  // server→receiver: { sender_uid, file_key, filename, size, download_url, is_image }
    ID_FILE_MINIO_REFRESH_REQ  = 2035,  // receiver→server: 预签名URL过期，刷�?
    ID_FILE_MINIO_REFRESH_RSP  = 2036,  // server→receiver: { file_key, download_url }

    ID_HEARTBEAT = 2999,           // 心跳消息

    ID_WEBRTC_SIGNALING = 3001
};
enum ClickLbState{
    Normal = 0,
    Selected = 1
};
enum TipErr{
    TIP_SUCCESS = 0,
    TIP_EMAIL_ERR = 1,
    TIP_PWD_ERR = 2,
    TIP_CONFIRM_ERR = 3,
    TIP_PWD_CONFIRM = 4,
    TIP_VARIFY_ERR = 5,
    TIP_USER_ERR = 6
};
// 错误码定义（与服务端保持一致）
enum ErrorCodes{
    Success = 0,
    Error_Json = 1001,      // JSON解析错误
    RPCFailed = 1002,       // RPC请求错误  
    Varify_error = 1003,    // 验证码错�?
    VarifyExpired = 1004,   // 验证码过�?
    UserExist = 1005,       // 用户已存�?
    UserNotExist = 1006,    // 用户不存�?
    EmailNotMatch = 1007,   // 邮箱和用户名不匹�?
    PasswdUpFailed = 1008,  // 密码更新失败
    PasswdInvalid = 1009,   // 密码错误
    UserOffline = 1010,     // 目标用户不在线
    SearchNotFound = 1011,  // 搜索无结果
    TokenInvalid = 1012,    // token无效或已过期
};
enum Modules{
    REGISTERMOD = 0,
    RESETPWDMOD = 1,
    USERLOGIN = 2,
    SEARCHMOD = 3,
};
enum ChatUIMode{
    ChatMode = 0,
    ContactMode = 1,
    SearchMode = 2,
};

#endif // GLOBAL_H
