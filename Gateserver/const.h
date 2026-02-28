//
// Created by EDPerson on 2026/2/3.
//

#ifndef WXCHAT_CONST_H
#define WXCHAT_CONST_H
#include<memory>
#include <iostream>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include<json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <unordered_map>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <queue>
#include <string>
#include "ConfigMgr.h"
class ConfigMgr;
extern ConfigMgr gCfgMgr;

enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,  //Json解析错误
    RPCFailed = 1002,  //RPC请求错误
    Varify_error = 1003,//验证码错误
    VarifyExpired = 1004,//验证码不存在
    UserExist = 1005,//用户已存在
    UserNotExist = 1006,//用户不存在
    EmailNotMatch = 1007,//邮箱和用户名不匹配
    PasswdUpFailed = 1008,//密码更新失败
    PasswdInvalid = 1009,//密码错误
    UserOffline = 1010,     //目标用户不在线
    SearchNotFound = 1011, //搜索无结果
    TokenInvalid = 1012,   //token无效或已过期
};

// Redis键名前缀常量（需要与VarifyServer保持一致）
const std::string CODE_PREFIX = "code_";

#pragma once
#include <string> // 别忘了包含这个

// 定义用户信息结构体
struct UserInfo {
    std::string name;
    std::string pwd;
    std::string email;
    int uid = 0;
};
#endif //WXCHAT_CONST_H