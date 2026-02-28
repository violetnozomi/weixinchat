//
// Created by EDPerson on 2026/2/13.
//

#ifndef CHATSERVER_CONST_H
#define CHATSERVER_CONST_H
#include <unordered_map>
#include <memory>
#include <iostream>
#include <string>
#include <queue>
#include <mutex>
#include <json/json.h>

// 消息ID定义（与客户端保持一致）
enum MSG_ID {
    // TCP聊天消息 (2000-2999)
    ID_CHAT_LOGIN_REQ = 2001,      // 聊天服务器认证请�?
    ID_CHAT_LOGIN_RSP = 2002,      // 聊天服务器认证响�?
    ID_TEXT_CHAT_MSG = 2003,       // 发送文本消�?
    ID_TEXT_CHAT_MSG_RSP = 2004,   // 文本消息响应
    ID_SEARCH_USER_REQ = 2005,     // 搜索用户请求
    ID_SEARCH_USER_RSP = 2006,     // 搜索用户响应
    ID_ADD_FRIEND_REQ = 2007,          // 添加好友请求
    ID_ADD_FRIEND_RSP = 2008,          // 添加好友响应
    ID_NOTIFY_ADD_FRIEND = 2009,       // 好友申请通知（被动接收）
    ID_ACCEPT_FRIEND_REQ = 2010,       // 同意好友申请请求
    ID_ACCEPT_FRIEND_RSP = 2011,       // 同意好友申请响应（发送方收到：对方已接受�?
    ID_NOTIFY_ACCEPT_FRIEND = 2012,    // 好友已同意通知（推给申请发起方�?
    ID_FRIENDS_LIST_RSP = 2013,        // 登录时服务端推送好友列�?

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
    // 流程：client 请求上传URL �?直传MinIO �?通知服务端提�?�?服务端写DB+通知接收�?
    ID_FILE_MINIO_INIT_REQ     = 2030,  // client→server: 申请上传预签�?PUT URL
    ID_FILE_MINIO_INIT_RSP     = 2031,  // server→client: { file_key, upload_url }
    ID_FILE_MINIO_COMMIT       = 2032,  // client→server: 文件已上传到MinIO，提交记�?
    ID_FILE_MINIO_COMMIT_RSP   = 2033,  // server→client: { error }
    ID_FILE_MINIO_NOTIFY       = 2034,  // server→receiver: { sender_uid, file_key, filename, size, download_url, is_image }
    ID_FILE_MINIO_REFRESH_REQ  = 2035,  // receiver→server: 预签名URL已过期，需要刷�?
    ID_FILE_MINIO_REFRESH_RSP  = 2036,  // server→receiver: { file_key, download_url }

    ID_HEARTBEAT = 2999,               // 心跳消息

    ID_WEBRTC_SIGNALING = 3001,
};

// 错误码定义（与wxchat/GrpcStatusServer保持一致）
enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,  //Json解析错误
    RPCFailed = 1002,  //RPC请求错误
    Varify_error = 1003,//验证码错�?
    VarifyExpired = 1004,//验证码不存在
    UserExist = 1005,//用户已存�?
    UserNotExist = 1006,//用户不存�?
    EmailNotMatch = 1007,//邮箱和用户名不匹�?
    PasswdUpFailed = 1008,//密码更新失败
    PasswdInvalid = 1009,//密码错误
    UserOffline = 1010,  //目标用户不在�?
    SearchNotFound = 1011, //搜索无结�?
};

#endif //CHATSERVER_CONST_H
