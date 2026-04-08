//
// Created by EDPerson on 2026/2/3.
//

#include "LogicSystem.h"
#include "HttpConnection.h"
#include "VaiifyGrpcClient.h"
#include "StatusGrpcClient.h"
#include "RedisMgr.h"

// ─────────────────────────────────────────────────────────────────────────────
// HTTP 鉴权工具：验证请求体中的 uid + token 是否与 Redis 中的记录匹配
// 存储格式：Redis key "token:<token>" → uid字符串，由 GrpcStatusServer 登录时写入
// 返回 true=验证通过；返回 false 时 err_root 已填入错误码，直接序列化响应即可
// ─────────────────────────────────────────────────────────────────────────────
static bool VerifyHttpToken(const Json::Value& src, Json::Value& err_root) {
    if (!src.isMember("uid") || !src.isMember("token") ||
        src["token"].asString().empty()) {
        err_root["error"] = ErrorCodes::TokenInvalid;
        err_root["msg"]   = "Missing uid or token";
        return false;
    }
    int         uid   = src["uid"].asInt();
    std::string token = src["token"].asString();
    std::string stored_uid;
    bool found = RedisMgr::GetInstance()->Get("token:" + token, stored_uid);
    if (!found || std::to_string(uid) != stored_uid) {
        err_root["error"] = ErrorCodes::TokenInvalid;
        err_root["msg"]   = "Token invalid or expired";
        return false;
    }
    return true;
}
#include "MySqlPool.h"
LogicSystem::LogicSystem() {
    RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
       beast::ostream(connection->_response.body()) << "receive get_test req";
        int i = 0;
        for (auto& elem : connection->_get_params) {
           i++;
           beast::ostream(connection->_response.body()) << "param" << i << " key is " << elem.first;
           beast::ostream(connection->_response.body()) << ", " <<  " value is " << elem.second << std::endl;
       }
   });
    RegPost("/get_varifycode",[](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout<<"Received POST data: "<<body_str<<std::endl;
        connection->_response.set(http::field::content_type, "application/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parsing_successful = reader.parse(body_str, src_root);
        if (!parsing_successful) {
            root["error"] = ErrorCodes::Error_Json;
            std:: string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }
        auto email = src_root["email"].asString();
        if (email.empty() || email.size() > 254) {
            root["error"] = ErrorCodes::Error_Json;
            root["msg"] = "Invalid email";
            beast::ostream(connection->_response.body()) << root.toStyledString();
            return true;
        }
        GetVarifyRsp rsp = VaiifyGrpcClient::GetInstance()->GetVarifyCode(email);
        std::cout << root["email"].asString() << std::endl;
        root["error"] = 0;
        root["email"] = src_root["email"];
        std::string reply = root.toStyledString();
        beast::ostream(connection->_response.body()) << reply;
        return true;
    });
    RegPost("/user_register",[](std::shared_ptr<HttpConnection> connection) {

        auto bodystr = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout<<"Received POST data: "<<bodystr<<std::endl;
        connection->_response.set(http::field::content_type,"application/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parsing_successful = reader.parse(bodystr, src_root);
        if (!parsing_successful) {
            root["error"] = ErrorCodes::Error_Json;
            std:: string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }

        // 输入验证
        auto user = src_root["user"].asString();
        auto passwd = src_root["passwd"].asString();
        auto email = src_root["email"].asString();
        if (user.empty() || user.size() > 64) {
            root["error"] = ErrorCodes::Error_Json;
            root["msg"] = "Username must be 1-64 characters";
            beast::ostream(connection->_response.body()) << root.toStyledString();
            return true;
        }
        if (passwd.empty() || passwd.size() < 6) {
            root["error"] = ErrorCodes::Error_Json;
            root["msg"] = "Password must be at least 6 characters";
            beast::ostream(connection->_response.body()) << root.toStyledString();
            return true;
        }
        if (email.empty() || email.size() > 254) {
            root["error"] = ErrorCodes::Error_Json;
            root["msg"] = "Invalid email";
            beast::ostream(connection->_response.body()) << root.toStyledString();
            return true;
        }

        std::string VarifyCode ;
        // 使用CODE_PREFIX前缀获取验证码（与VarifyServer保持一致）
        std::string email = src_root["email"].asString();
        bool get_successful = RedisMgr::GetInstance()->Get(CODE_PREFIX + email, VarifyCode);
        if (!get_successful) {
       root["error"] = ErrorCodes::VarifyExpired;
       std::string jsonstr = root.toStyledString();
       beast::ostream(connection->_response.body()) << jsonstr;
       return true;
   }
        if (src_root["varifycode"].asString() != VarifyCode) {
            root["error"] = ErrorCodes::Varify_error;
            std::string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }
        bool b_usr_exist = RedisMgr::GetInstance()->ExistsKey(src_root["user"].asString());
    if (b_usr_exist) {
        std::cout << " user exist" << std::endl;
        root["error"] = ErrorCodes::UserExist;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;
        return true;
    }

    //查找数据库判断用户是否存在
    MysqlDao mysql_dao;
    int uid = mysql_dao.RegUser(src_root["user"].asString(),src_root["email"].asString(),src_root["passwd"].asString());
        if (uid > 0) {
        root["error"] = 0;
        root["uid"] = uid;
        root["email"] = src_root["email"].asString();
        root["user"] = src_root["user"].asString();
        root["msg"] = "ok";
    }
    else if (uid == 0) {
        root["error"] = 1;
        root["msg"] = "User name already exists";
    }
    else if (uid == -2) {
        root["error"] = 2;
        root["msg"] = "Email already exists";
    }
    else {
        root["error"] = -1;
        root["msg"] = "Server internal error";
    }
    std::cout << root.toStyledString() << std::endl;
    std::string jsonstr = root.toStyledString();
    beast::ostream(connection->_response.body()) << jsonstr;
    return true;
    });
    RegPost("/reset_pwd",[](std::shared_ptr<HttpConnection> connection) {
        auto bodystr = beast::buffers_to_string(connection->_request.body().data());
        std::cout<<"Received POST data: "<<bodystr<<std::endl;
        connection->_response.set(http::field::content_type,"application/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        
        // 1. 解析JSON - 必须先解析再使用！
        bool parsing_successful = reader.parse(bodystr, src_root);
        if (!parsing_successful) {
            std::cout << "JSON解析失败" << std::endl;
            root["error"] = ErrorCodes::Error_Json;
            std::string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }
        
        // 2. 提取参数
        auto user = src_root["user"].asString();
        auto email = src_root["email"].asString();
        auto new_passwd = src_root["new_pwd"].asString();
        auto varifycode = src_root["varifycode"].asString();
        
        // 3. 验证验证码是否存在和正确
        std::string VarifyCode;
        bool get_successful = RedisMgr::GetInstance()->Get(CODE_PREFIX + email, VarifyCode);
        if (!get_successful) {
            std::cout << "验证码过期或不存在: email=" << email << std::endl;
            root["error"] = ErrorCodes::VarifyExpired;
            std::string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }
        
        if (varifycode != VarifyCode) {
            std::cout << "验证码错误: 输入=" << varifycode << ", 正确=" << VarifyCode << std::endl;
            root["error"] = ErrorCodes::Varify_error;
            std::string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }
        
        // 4. 从数据库查询用户名对应的邮箱
        MysqlDao mysql_dao;
        auto get_email = mysql_dao.GetEmailByName(user);
        
        // 5. 检查用户是否存在
        if (get_email.empty() || get_email == "") {
            std::cout << "用户不存在: user=" << user << std::endl;
            root["error"] = ErrorCodes::UserNotExist;
            root["msg"] = "User does not exist";
            std::string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }
        
        // 6. 验证邮箱和用户名是否匹配
        if (get_email != email) {
            std::cout << "邮箱不匹配: 用户=" << user << ", 输入邮箱=" << email 
                      << ", 数据库邮箱=" << get_email << std::endl;
            root["error"] = ErrorCodes::EmailNotMatch;
            root["msg"] = "Email does not match username";
            std::string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }
        
        // 7. 更新密码
        bool reset_success = mysql_dao.ResetPwd(user, new_passwd);
        if (!reset_success) {
            std::cout << "密码更新失败: user=" << user << std::endl;
            root["error"] = ErrorCodes::PasswdUpFailed;
            root["msg"] = "Password update failed";
            std::string reply = root.toStyledString();
            beast::ostream(connection->_response.body()) << reply;
            return true;
        }
        
        // 8. 成功！返回成功响应
        std::cout << "密码重置成功: user=" << user << std::endl;
        root["error"] = ErrorCodes::Success;
        root["msg"] = "Password reset successfully";
        root["user"] = user;
        root["email"] = email;
        std::string reply = root.toStyledString();
        beast::ostream(connection->_response.body()) << reply;
        return true;
    });
    RegPost("/user_login",[](std::shared_ptr<HttpConnection> connection) {
       auto bodystr = beast::buffers_to_string(connection->_request.body().data());
       std::cout<<"Received POST data: "<<bodystr<<std::endl;
       connection->_response.set(http::field::content_type, "application/json");
       Json::Value root;
       Json::Reader reader;
       Json::Value src_root;
       bool parse_successful = reader.parse(bodystr, src_root);
       if (!parse_successful) {
           root["error"] = ErrorCodes::Error_Json;
           auto reply = root.toStyledString();
           beast::ostream(connection->_response.body())<<reply;
           return true;
       }
       
       // 提取用户名/邮箱和密码
       auto user = src_root["user"].asString();
       auto pwd = src_root["pwd"].asString();
       
       // 验证输入
       if (user.empty() || pwd.empty()) {
           root["error"] = ErrorCodes::Error_Json;
           root["msg"] = "User or password cannot be empty";
           std::string reply = root.toStyledString();
           beast::ostream(connection->_response.body()) << reply;
           return true;
       }
       
       // 查询数据库验证用户名/邮箱和密码
       UserInfo userInfo;
       std::shared_ptr<MysqlDao> mysql_dao = std::make_shared<MysqlDao>();
       bool check_result = mysql_dao->CheckUserPwd(user, pwd, userInfo);
       
       if (!check_result) {
           // 用户不存在或密码错误
           root["error"] = ErrorCodes::PasswdInvalid;
           root["msg"] = "User does not exist or password is incorrect";
           std::string reply = root.toStyledString();
           beast::ostream(connection->_response.body()) << reply;
           return true;
       }
       
       // 查询StatusServer获取合适的ChatServer连接
       auto reply = StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid);
       if (reply.error() != ErrorCodes::Success) {
           std::cerr << "获取ChatServer失败: uid=" << userInfo.uid << std::endl;
           root["error"] = ErrorCodes::RPCFailed;
           root["msg"] = "Failed to get chat server";
           std::string jsonstr = root.toStyledString();
           beast::ostream(connection->_response.body()) << jsonstr;
           return true;
       }
       
       std::cout << "StatusServer返回: host=" << reply.host() 
                 << ", port=" << reply.port() 
                 << ", token=" << reply.token() << std::endl;
       
       // 登录成功，返回用户信息和连接信息
       root["error"] = ErrorCodes::Success;
       root["user"] = userInfo.name;
       root["uid"] = userInfo.uid;
       root["token"] = reply.token();
       root["host"] = reply.host();
       root["port"] = reply.port();
       root["msg"] = "Login successful";
       
       std::string replystr = root.toStyledString();
       beast::ostream(connection->_response.body()) << replystr;
       std::cout << "Login successful: uid=" << userInfo.uid 
                 << ", name=" << userInfo.name << std::endl;
       return true;
    });

    // ── 搜索用户 ────────────────────────────────────────────────────────
    // POST /search_user
    // 请求体: { "uid": <请求者uid>, "keyword": "<搜索关键词>" }
    // 响应体: { "error": 0, "users": [ { "uid": N, "name": "..." }, ... ] }
    RegPost("/search_user", [](std::shared_ptr<HttpConnection> connection) {
        auto bodystr = beast::buffers_to_string(connection->_request.body().data());
        connection->_response.set(http::field::content_type, "application/json");

        Json::Value root;
        Json::Reader reader;
        Json::Value src;
        if (!reader.parse(bodystr, src)) {
            root["error"] = ErrorCodes::Error_Json;
            beast::ostream(connection->_response.body()) << root.toStyledString();
            return true;
        }

        // ── 鉴权：验证 uid + token ──
        if (!VerifyHttpToken(src, root)) {
            beast::ostream(connection->_response.body()) << root.toStyledString();
            return true;
        }

        std::string keyword = src["keyword"].asString();
        if (keyword.empty()) {
            root["error"] = ErrorCodes::Error_Json;
            root["msg"] = "keyword cannot be empty";
            beast::ostream(connection->_response.body()) << root.toStyledString();
            return true;
        }

        MysqlDao mysql_dao;
        auto users = mysql_dao.SearchUser(keyword, 20);

        if (users.empty()) {
            root["error"] = ErrorCodes::SearchNotFound;
            root["msg"] = "No users found";
            root["users"] = Json::Value(Json::arrayValue);
            beast::ostream(connection->_response.body()) << root.toStyledString();
            return true;
        }

        Json::Value user_array(Json::arrayValue);
        for (const auto& u : users) {
            Json::Value item;
            item["uid"]  = u.uid;
            item["name"] = u.name;
            user_array.append(item);
        }
        root["error"] = ErrorCodes::Success;
        root["users"] = user_array;
        beast::ostream(connection->_response.body()) << root.toStyledString();
        return true;
    });
}

void LogicSystem::RegGet(std::string url, HttpHandler handler) {
    _get_handler.insert(std::make_pair(url,handler));
}
bool LogicSystem::HandleGet(std::string url, std::shared_ptr<HttpConnection> con) {
    if (_get_handler.find(url) == _get_handler.end()) {
        return false;
    }
    _get_handler[url](con);
    return true;
}
bool LogicSystem::HandlePost(std::string url , std::shared_ptr<HttpConnection> con) {
    if (_post_handler.find(url) == _post_handler.end()) {
        return false;
    }
    _post_handler[url](con);
    return true;
}

void LogicSystem::RegPost(std::string url, HttpHandler handler) {
    _post_handler.insert(std::make_pair(url,handler));
}

