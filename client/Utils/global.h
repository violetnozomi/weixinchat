#ifndef GLOBAL_H
#define GLOBAL_H

#include <QRegularExpressionMatch>
#include <QStyle>
#include <QWidget>
#include <functional>
#include <memory.h>
#include <mutex>
#include <QObject>
#include <QString>
#include <unordered_map>

extern std::function<void(QWidget*)> repolish;
extern QString gate_url_prefix;

enum REQ_ID {
    // HTTP
    ID_GET_VERIFY_CODE = 1001,
    ID_REGISTER_USER = 1002,
    ID_RESET_PWD = 1003,
    ID_LOGIN_USER = 1004,
    ID_SEARCH_USER = 1005,

    // TCP chat
    ID_CHAT_LOGIN_REQ = 2001,
    ID_CHAT_LOGIN_RSP = 2002,
    ID_TEXT_CHAT_MSG = 2003,
    ID_TEXT_CHAT_MSG_RSP = 2004,
    ID_SEARCH_USER_REQ = 2005,
    ID_SEARCH_USER_RSP = 2006,
    ID_ADD_FRIEND_REQ = 2007,
    ID_ADD_FRIEND_RSP = 2008,
    ID_NOTIFY_ADD_FRIEND = 2009,
    ID_ACCEPT_FRIEND_REQ = 2010,
    ID_ACCEPT_FRIEND_RSP = 2011,
    ID_NOTIFY_ACCEPT_FRIEND = 2012,
    ID_FRIENDS_LIST_RSP = 2013,

    ID_DELETE_FRIEND_REQ = 2023,
    ID_DELETE_FRIEND_RSP = 2024,
    ID_BLOCK_FRIEND_REQ = 2025,
    ID_BLOCK_FRIEND_RSP = 2026,
    ID_UPDATE_PROFILE_REQ = 2027,
    ID_UPDATE_PROFILE_RSP = 2028,
    ID_AI_CHAT_MSG = 2029,

    ID_FILE_MINIO_INIT_REQ = 2030,
    ID_FILE_MINIO_INIT_RSP = 2031,
    ID_FILE_MINIO_COMMIT = 2032,
    ID_FILE_MINIO_COMMIT_RSP = 2033,
    ID_FILE_MINIO_NOTIFY = 2034,
    ID_FILE_MINIO_REFRESH_REQ = 2035,
    ID_FILE_MINIO_REFRESH_RSP = 2036,
    ID_FILE_MINIO_MP_INIT_REQ = 2037,
    ID_FILE_MINIO_MP_INIT_RSP = 2038,
    ID_FILE_MINIO_MP_COMPLETE_REQ = 2039,
    ID_FILE_MINIO_MP_COMPLETE_RSP = 2040,
    ID_FILE_MINIO_MP_RESUME_REQ = 2041,
    ID_FILE_MINIO_MP_RESUME_RSP = 2042,
    ID_FILE_MINIO_MP_ABORT_REQ = 2043,

    ID_SYNC_MSG_REQ = 2050,
    ID_SYNC_MSG_RSP = 2051,

    ID_CREATE_GROUP_REQ = 2060,
    ID_CREATE_GROUP_RSP = 2061,
    ID_GROUP_INFO_REQ = 2062,
    ID_GROUP_INFO_RSP = 2063,
    ID_GROUP_MEMBER_LIST_REQ = 2064,
    ID_GROUP_MEMBER_LIST_RSP = 2065,
    ID_INVITE_GROUP_MEMBER_REQ = 2066,
    ID_INVITE_GROUP_MEMBER_RSP = 2067,
    ID_QUIT_GROUP_REQ = 2068,
    ID_QUIT_GROUP_RSP = 2069,
    ID_GROUP_TEXT_MSG = 2070,
    ID_GROUP_TEXT_MSG_RSP = 2071,
    ID_GROUP_SYNC_REQ = 2072,
    ID_GROUP_SYNC_RSP = 2073,
    ID_GROUP_EVENT_NOTIFY = 2074,

    ID_HEARTBEAT = 2999,
    ID_WEBRTC_SIGNALING = 3001
};

enum ClickLbState {
    Normal = 0,
    Selected = 1
};

enum TipErr {
    TIP_SUCCESS = 0,
    TIP_EMAIL_ERR = 1,
    TIP_PWD_ERR = 2,
    TIP_CONFIRM_ERR = 3,
    TIP_PWD_CONFIRM = 4,
    TIP_VARIFY_ERR = 5,
    TIP_USER_ERR = 6
};

enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,
    RPCFailed = 1002,
    Varify_error = 1003,
    VarifyExpired = 1004,
    UserExist = 1005,
    UserNotExist = 1006,
    EmailNotMatch = 1007,
    PasswdUpFailed = 1008,
    PasswdInvalid = 1009,
    UserOffline = 1010,
    SearchNotFound = 1011,
    TokenInvalid = 1012,
    GroupNotFound = 1013,
    PermissionDenied = 1014,
    AlreadyInGroup = 1015,
    GroupMuted = 1016,
    NotImplementedYet = 1017,
    InvalidRequest = 1018,
};

enum Modules {
    REGISTERMOD = 0,
    RESETPWDMOD = 1,
    USERLOGIN = 2,
    SEARCHMOD = 3,
};

enum ChatUIMode {
    ChatMode = 0,
    ContactMode = 1,
    SearchMode = 2,
};

#endif // GLOBAL_H
