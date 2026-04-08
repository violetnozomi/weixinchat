// AgentManager.cpp  Phase-4: Streaming
#include "AgentManager.h"
#include "ConfigMgr.h"
#include "IOservicePool.h"
#include "SessionManager.h"
#include "CSession.h"
#include "MySqlPool.h"
#include "RedisMgr.h"
#include "DbWriteQueue.h"
#include "DbReadQueue.h"
#include "ChatLogicSystem.h"
#include "SyncService.h"
#include "const.h"
#include <boost/asio/post.hpp>
#include "SnowflakeID.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ssl/context.hpp>
#include <json/json.h>
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
namespace ssl   = net::ssl;
using     tcp   = net::ip::tcp;

namespace {

bool contains_any(const std::string& text, std::initializer_list<const char*> needles)
{
    for (const char* needle : needles) {
        if (needle != nullptr && *needle != '\0' && text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool should_force_search_chat_history(const std::string& text)
{
    const bool explicit_history = contains_any(
        text,
        {"聊天记录", "聊天内容", "对话记录", "历史消息", "历史记录", "聊了什么", "说了什么", "最近聊"});
    const bool summary_request = contains_any(
        text,
        {"总结", "概括", "摘要", "回顾", "梳理", "整理", "归纳"});
    const bool chat_context = contains_any(
        text,
        {"聊天", "对话", "消息", "记录", "内容", "聊过", "好友"});
    return explicit_history || (summary_request && chat_context);
}

bool should_reuse_last_history_peer(const std::string& text)
{
    return contains_any(text, {"继续", "接着", "然后", "后面", "补充", "展开", "延续", "上面", "刚才", "前面"});
}

int detect_forced_history_uid2(const std::string& text)
{
    if (contains_any(text, {"AI助手", "AI", "机器人", "助手"})) {
        return 0;
    }
    return -1;
}

void trim_json_newline(std::string& text)
{
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
}

std::string build_forced_history_tool_call_json(const std::string& friend_name,
                                                int uid2,
                                                int limit)
{
    Json::Value args(Json::objectValue);
    if (uid2 >= 0) {
        args["uid2"] = uid2;
    }
    if (!friend_name.empty()) {
        args["friend_name"] = friend_name;
    }
    args["limit"] = limit;

    Json::FastWriter fw;
    std::string args_json = fw.write(args);
    trim_json_newline(args_json);

    Json::Value tc_arr(Json::arrayValue);
    Json::Value tc(Json::objectValue);
    tc["id"] = "forced_search_chat_history";
    tc["type"] = "function";
    tc["function"]["name"] = "search_chat_history";
    tc["function"]["arguments"] = args_json;
    tc_arr.append(tc);

    std::string tc_json = fw.write(tc_arr);
    trim_json_newline(tc_json);
    return tc_json;
}

/*
constexpr const char* kActionDraftSendText = "draft_send_text_message";
constexpr const char* kActionSendTextMessage = "send_text_message";
constexpr const char* kDraftPrefixCn = "草稿：";
constexpr const char* kDraftPrefixEn = "Draft:";

std::string trim_copy(const std::string& text)
{
    std::size_t start = 0;
    while (start < text.size() &&
           (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' ||
            text[start] == '\n')) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
            text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(start, end - start);
}

std::string to_ascii_lower(std::string text)
{
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}

bool should_draft_reply_message(const std::string& text)
{
    return contains_any(text,
                        {"帮我回复",
                         "帮我回",
                         "替我回复",
                         "替我回",
                         "代我回复",
                         "代我回",
                         "帮我发",
                         "替我发",
                         "代我发",
                         "帮我给",
                         "替我给",
                         "发给他",
                         "发给她",
                         "告诉他",
                         "告诉她",
                         "reply to",
                         "/draft"});
}

bool is_confirm_send_text(const std::string& text)
{
    const std::string trimmed = trim_copy(text);
    const std::string lower = to_ascii_lower(trimmed);
    return trimmed == "确认发送" ||
           trimmed == "确认" ||
           trimmed == "发送吧" ||
           trimmed == "就发这个" ||
           trimmed == "就发这条" ||
           trimmed == "按这个发" ||
           lower == "/send" ||
           lower == "/confirm" ||
           lower == "yes" ||
           lower == "ok";
}

bool is_cancel_send_text(const std::string& text)
{
    const std::string trimmed = trim_copy(text);
    const std::string lower = to_ascii_lower(trimmed);
    return trimmed == "取消发送" ||
           trimmed == "取消" ||
           trimmed == "别发了" ||
           trimmed == "先别发" ||
           trimmed == "算了" ||
           trimmed == "不用发了" ||
           lower == "/cancel" ||
           lower == "/stop" ||
           lower == "no";
}

std::string extract_draft_body(const std::string& text)
{
    std::string draft = trim_copy(text);
    if (draft.rfind(kDraftPrefixCn, 0) == 0) {
        return trim_copy(draft.substr(std::string(kDraftPrefixCn).size()));
    }
    if (draft.rfind(kDraftPrefixEn, 0) == 0) {
        return trim_copy(draft.substr(std::string(kDraftPrefixEn).size()));
    }
    return std::string();
}

*/

constexpr const char* kActionDraftSendText = "draft_send_text_message";
constexpr const char* kActionSendTextMessage = "send_text_message";
constexpr const char* kDraftPrefix = "Draft:";
constexpr const char* kDraftPrefixCn = "草稿";
constexpr const char* kFullWidthColon = "：";

std::string trim_copy(const std::string& text)
{
    std::size_t start = 0;
    while (start < text.size() &&
           (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' ||
            text[start] == '\n')) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
            text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(start, end - start);
}

std::string to_ascii_lower(std::string text)
{
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}

std::string strip_code_fences(std::string text)
{
    text = trim_copy(text);
    if (text.rfind("```", 0) != 0) {
        return text;
    }

    const std::size_t first_newline = text.find('\n');
    if (first_newline == std::string::npos) {
        return text;
    }

    const std::size_t closing = text.rfind("```");
    if (closing == std::string::npos || closing <= first_newline) {
        return text;
    }

    return trim_copy(text.substr(first_newline + 1, closing - first_newline - 1));
}

std::string extract_prefixed_draft_body(std::string text)
{
    text = trim_copy(text);
    if (text.empty()) {
        return std::string();
    }

    if (text.rfind(kDraftPrefixCn, 0) == 0) {
        std::string body = trim_copy(text.substr(std::string(kDraftPrefixCn).size()));
        if (body.rfind(kFullWidthColon, 0) == 0) {
            body = trim_copy(body.substr(std::string(kFullWidthColon).size()));
        }
        if (!body.empty() && (body[0] == ':' || body[0] == '-' || body[0] == '>')) {
            body = trim_copy(body.substr(1));
        }
        return body;
    }

    std::string lower = to_ascii_lower(text);
    if (lower.rfind("draft", 0) == 0) {
        std::size_t pos = 5;
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\t' || text[pos] == ':' ||
                text[pos] == '-' || text[pos] == '>')) {
            ++pos;
        }
        std::string body = trim_copy(text.substr(pos));
        if (body.rfind(kFullWidthColon, 0) == 0) {
            body = trim_copy(body.substr(std::string(kFullWidthColon).size()));
        }
        return body;
    }

    return std::string();
}

bool is_likely_draft_clarification(const std::string& text)
{
    const std::string lower = to_ascii_lower(trim_copy(text));
    return text.find('?') != std::string::npos ||
           text.find("？") != std::string::npos ||
           lower.find("clarify") != std::string::npos ||
           lower.find("which") != std::string::npos ||
           lower.find("tone") != std::string::npos ||
           lower.find("style") != std::string::npos ||
           lower.find("specify") != std::string::npos ||
           lower.find("need more") != std::string::npos ||
           lower.find("insufficient") != std::string::npos;
}

bool is_reasonable_draft_body_candidate(const std::string& text)
{
    const std::string trimmed = trim_copy(text);
    const std::string lower = to_ascii_lower(trimmed);
    if (trimmed.empty() || trimmed.size() > 300) {
        return false;
    }
    if (lower.find("/send") != std::string::npos ||
        lower.find("/cancel") != std::string::npos ||
        lower.find("draft ready") != std::string::npos ||
        lower.find("message sent") != std::string::npos ||
        lower.find("message saved") != std::string::npos ||
        lower.find("server time") != std::string::npos ||
        lower.find("delivery:") != std::string::npos) {
        return false;
    }
    if (is_likely_draft_clarification(trimmed)) {
        return false;
    }

    std::size_t newline_count = 0;
    for (char ch : trimmed) {
        if (ch == '\n') {
            ++newline_count;
        }
    }
    return newline_count <= 2;
}

bool should_draft_reply_message(const std::string& text)
{
    const bool direct_send_phrase = contains_any(
        text,
        {"帮我回复",
         "替我回复",
         "代我回复",
         "帮我发",
         "替我发",
         "代我发",
         "帮我给",
         "替我给",
         "代我给",
         "发给",
         "给他发",
         "给她发",
         "给ta发",
         "给TA发",
         "告诉他",
         "告诉她",
         "告诉ta",
         "告诉TA",
         "转告他",
         "转告她",
         "转告ta",
         "转告TA",
         "reply",
         "send for me",
         "reply for me",
         "draft",
         "message for",
         "reply to",
         "/draft"});

    const bool generic_send_shape =
        (text.find("给") != std::string::npos && text.find("发") != std::string::npos);

    return direct_send_phrase || generic_send_shape;
}

bool is_confirm_send_text(const std::string& text)
{
    const std::string trimmed = trim_copy(text);
    const std::string lower = to_ascii_lower(trimmed);
    return trimmed == "确认发送" ||
           trimmed == "确认" ||
           trimmed == "发送吧" ||
           trimmed == "就发这个" ||
           trimmed == "就发这条" ||
           trimmed == "按这个发" ||
           lower == "confirm" ||
           lower == "send" ||
           lower == "/send" ||
           lower == "/confirm" ||
           lower == "yes" ||
           lower == "ok";
}

bool is_cancel_send_text(const std::string& text)
{
    const std::string trimmed = trim_copy(text);
    const std::string lower = to_ascii_lower(trimmed);
    return trimmed == "取消发送" ||
           trimmed == "取消" ||
           trimmed == "别发了" ||
           trimmed == "先别发" ||
           trimmed == "算了" ||
           trimmed == "不用发了" ||
           lower == "cancel" ||
           lower == "stop" ||
           lower == "/cancel" ||
           lower == "/stop" ||
           lower == "no";
}

std::string extract_draft_body(const std::string& text)
{
    std::string draft = strip_code_fences(text);

    if (const std::string body = extract_prefixed_draft_body(draft); !body.empty()) {
        return body;
    }

    std::size_t start = 0;
    while (start < draft.size()) {
        const std::size_t end = draft.find('\n', start);
        const std::string line = draft.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (const std::string body = extract_prefixed_draft_body(line); !body.empty()) {
            return body;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    if (is_reasonable_draft_body_candidate(draft)) {
        return draft;
    }

    return std::string();
}

std::string build_send_payload_json(int peer_uid, const std::string& content)
{
    Json::Value payload(Json::objectValue);
    payload["peer_uid"] = peer_uid;
    payload["content"] = content;
    Json::FastWriter fw;
    std::string text = fw.write(payload);
    trim_json_newline(text);
    return text;
}

bool parse_send_payload_json(const std::string& payload_json,
                             int& peer_uid,
                             std::string& content)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(payload_json, root)) {
        return false;
    }
    peer_uid = root.get("peer_uid", 0).asInt();
    content = root.get("content", "").asString();
    return peer_uid > 0 && !content.empty();
}

std::string get_user_display_name(int uid)
{
    UserInfo info;
    MysqlDao dao;
    if (dao.GetUserByUid(uid, info) && !info.name.empty()) {
        return info.name;
    }
    return "uid_" + std::to_string(uid);
}

std::string format_server_time(std::time_t t)
{
    std::tm tv{};
#ifdef _WIN32
    localtime_s(&tv, &t);
#else
    localtime_r(&t, &tv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tv);
    return buf;
}

bool strip_prefix_once(std::string& text, std::initializer_list<const char*> prefixes)
{
    text = trim_copy(text);
    for (const char* prefix : prefixes) {
        if (prefix == nullptr || *prefix == '\0') {
            continue;
        }
        const std::string needle(prefix);
        if (text.rfind(needle, 0) == 0) {
            text = trim_copy(text.substr(needle.size()));
            return true;
        }
    }
    return false;
}

std::string strip_wrapping_quotes(std::string text)
{
    text = trim_copy(text);
    bool changed = true;
    while (changed && !text.empty()) {
        changed = false;
        if (text.size() >= 2 &&
            ((text.front() == '"' && text.back() == '"') ||
             (text.front() == '\'' && text.back() == '\''))) {
            text = trim_copy(text.substr(1, text.size() - 2));
            changed = true;
        }
        changed = strip_prefix_once(text, {"“", "”", "‘", "’"}) || changed;
        if (!text.empty()) {
            const std::string before = text;
            while (strip_prefix_once(text, {"“", "‘", "\"", "'" })) {
            }
            if (text != before) {
                changed = true;
            }
        }
        if (!text.empty()) {
            const std::string before = text;
            const std::array<std::string, 4> suffixes = {"”", "’", "\"", "'"};
            for (const auto& suffix : suffixes) {
                if (text.size() >= suffix.size() &&
                    text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    text = trim_copy(text.substr(0, text.size() - suffix.size()));
                    changed = true;
                    break;
                }
            }
            if (text != before) {
                changed = true;
            }
        }
    }
    return text;
}

bool try_build_direct_send_draft(int caller_uid,
                                 const std::string& text,
                                 int& peer_uid,
                                 std::string& draft)
{
    MysqlDao dao;
    std::vector<UserInfo> friends;
    if (!dao.GetFriendsByUid(caller_uid, friends)) {
        return false;
    }

    std::vector<UserInfo> matched;
    for (const auto& f : friends) {
        if (f.name.empty()) {
            continue;
        }
        if (text.find(f.name) != std::string::npos) {
            matched.push_back(f);
        }
    }
    if (matched.size() != 1) {
        return false;
    }

    peer_uid = matched.front().uid;
    const std::string& peer_name = matched.front().name;
    const std::size_t name_pos = text.find(peer_name);
    if (name_pos == std::string::npos) {
        return false;
    }

    std::string body = trim_copy(text.substr(name_pos + peer_name.size()));
    if (body.empty()) {
        return false;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        changed = strip_prefix_once(body, {"，", ",", "：", ":", "。", ".", "-", ">", "给", "和"}) || changed;
        changed = strip_prefix_once(body, {"发送", "发", "回复", "回", "说", "转告", "转达",
                                           "message", "reply", "say"}) || changed;
        changed = strip_prefix_once(body, {"一下", "一个", "一句", "一条", "一段", "一则"}) || changed;
    }

    body = strip_wrapping_quotes(body);
    body = trim_copy(body);
    if (body.empty()) {
        return false;
    }

    const std::string lower = to_ascii_lower(body);
    if (lower == "send" || lower == "reply" || lower == "message") {
        return false;
    }

    draft = body;
    return true;
}

std::string build_send_confirm_note(int peer_uid, const std::string& draft)
{
    std::string preview = trim_copy(draft);
    if (preview.size() > 80) {
        preview.resize(80);
        preview += "...";
    }
    return "Draft only for " + get_user_display_name(peer_uid) +
           ". It has not been sent yet. Reply /send to send it, or /cancel to cancel.\n"
           "Preview: " + preview;
}

struct SendTextResult {
    bool ok = false;
    bool delivered_live = false;
    std::time_t server_timestamp = 0;
    std::string error;
};

std::string build_send_result_note(int peer_uid, const SendTextResult& result)
{
    const std::string peer_name = get_user_display_name(peer_uid);
    const std::string server_time =
        format_server_time(result.server_timestamp > 0 ? result.server_timestamp
                                                       : std::time(nullptr));
    if (result.delivered_live) {
        return "Message sent to " + peer_name +
               " at " + server_time +
               " server time. Delivery: forwarded to an active session on this server. "
               "Read status updates after the recipient reads it.";
    }

    return "Message saved for " + peer_name +
           " at " + server_time +
           " server time. Delivery: queued for offline delivery.";
}

void emit_bot_message(int to_uid, const std::string& text)
{
    if (to_uid <= 0 || text.empty()) {
        return;
    }

    const std::string uid_str = std::to_string(to_uid);
    auto session = SessionManager::GetInstance()->GetSession(uid_str);
    if (!session) {
        DbWriteQueue::GetInstance()->PostWrite([to_uid, text]() {
            try {
                auto db = MysqlConPool::GetInstance()->GetSession();
                if (!db) {
                    return;
                }
                MysqlObj obj(std::move(db));
                obj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
                    .getTable("offline_messages")
                    .insert("id", "from_uid", "to_uid", "content", "type")
                    .values(NewSnowflakeID(), AgentManager::BOT_UID, to_uid, text, 0)
                    .execute();
            } catch (const std::exception& e) {
                std::cerr << "[Agent] emit_bot_message offline save error: "
                          << e.what() << std::endl;
            }
        });
        return;
    }

    static std::atomic<uint64_t> s_msg_seq{1};
    Json::Value msg;
    msg["msg_id"] = "9999_" + uid_str + "_" +
                    std::to_string(static_cast<Json::Int64>(::time(nullptr))) + "_" +
                    std::to_string(s_msg_seq.fetch_add(1));
    msg["from_uid"] = AgentManager::BOT_UID;
    msg["content"] = text;
    msg["timestamp"] = static_cast<Json::Int64>(::time(nullptr));

    Json::FastWriter fw;
    std::string js = fw.write(msg);
    trim_json_newline(js);
    session->Send(ID_AI_CHAT_MSG, js);
}

bool send_text_as_user(int from_uid,
                       int to_uid,
                       const std::string& content,
                       SendTextResult& result)
{
    result = SendTextResult{};
    if (from_uid <= 0 || to_uid <= 0) {
        result.error = "invalid uid";
        return false;
    }
    if (trim_copy(content).empty()) {
        result.error = "empty content";
        return false;
    }

    if (to_uid != from_uid && to_uid != AgentManager::BOT_UID) {
        MysqlDao dao_check;
        if (!dao_check.AreFriends(from_uid, to_uid)) {
            result.error = "target is not friend";
            return false;
        }
    }

    const std::time_t send_time = std::time(nullptr);
    result.server_timestamp = send_time;

    DbWriteQueue::GetInstance()->PostWrite([from_uid, to_uid, content]() {
        try {
            auto db = MysqlConPool::GetInstance()->GetSession();
            if (!db) {
                return;
            }
            MysqlObj obj(std::move(db));
            obj.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
                .getTable("chat_messages")
                .insert("id", "from_uid", "to_uid", "content")
                .values(NewSnowflakeID(), from_uid, to_uid, content)
                .execute();

            int64_t timeline_msg_id = 0;
            if (!SyncService::GetInstance()->saveMessage(
                    from_uid, to_uid, 0, content, timeline_msg_id)) {
                std::cerr << "[Agent] send_text_as_user timeline save error" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Agent] send_text_as_user chat_messages save error: "
                      << e.what() << std::endl;
        }
    });

    const TextRouteResult route_result =
        ChatLogicSystem::GetInstance()->RouteTextMessage(
            from_uid, to_uid, content, send_time);

    if (route_result.delivered_live) {
        std::cout << "[Agent] send_text_as_user live deliver from=" << from_uid
                  << " to=" << to_uid << std::endl;
        if (auto from_session =
                SessionManager::GetInstance()->GetSession(std::to_string(from_uid))) {
            Json::Value rsp;
            rsp["error"] = ErrorCodes::Success;
            rsp["echo_self"] = true;
            rsp["to_uid"] = to_uid;
            rsp["content"] = content;
            rsp["timestamp"] = static_cast<Json::Int64>(send_time);
            from_session->Send(ID_TEXT_CHAT_MSG_RSP, rsp.toStyledString());
        }
        result.ok = true;
        result.delivered_live = true;
        return true;
    }

    if (route_result.error != ErrorCodes::Success) {
        result.error = "route failed";
        std::cerr << "[Agent] send_text_as_user route error=" << route_result.error
                  << " from=" << from_uid << " to=" << to_uid << std::endl;
        return false;
    }

    if (route_result.queued_offline) {
        std::cout << "[Agent] send_text_as_user queued offline from=" << from_uid
                  << " to=" << to_uid << std::endl;
        if (auto from_session =
                SessionManager::GetInstance()->GetSession(std::to_string(from_uid))) {
            Json::Value rsp;
            rsp["error"] = ErrorCodes::Success;
            rsp["echo_self"] = true;
            rsp["to_uid"] = to_uid;
            rsp["content"] = content;
            rsp["timestamp"] = static_cast<Json::Int64>(send_time);
            from_session->Send(ID_TEXT_CHAT_MSG_RSP, rsp.toStyledString());
        }
        result.ok = true;
        result.delivered_live = false;
        return true;
    }

    result.error = "unknown route state";
    return false;
}

} // namespace

// ============================================================
// AIHttpSession  (Pass-1  stream=false)
// ============================================================
class AIHttpSession : public std::enable_shared_from_this<AIHttpSession> {
public:
    using AnswerCallback   = std::function<void(const std::string&)>;
    using ToolCallCallback = std::function<void(const Json::Value&)>;

    AIHttpSession(net::io_context& ioc, ssl::context& ctx, int uid,
                  std::string host, std::string port, std::string target,
                  std::string body, std::string key,
                  AnswerCallback ans, ToolCallCallback tc)
        : _resolver(net::make_strand(ioc)), _stream(net::make_strand(ioc), ctx)
        , _from_uid(uid), _host(std::move(host)), _port(std::move(port))
        , _target(std::move(target)), _body(std::move(body)), _api_key(std::move(key))
        , _on_answer(std::move(ans)), _on_tool_call(std::move(tc)) {}

    void Start() {
        if (!SSL_set_tlsext_host_name(_stream.native_handle(), _host.c_str())) return;
        _req.method(http::verb::post); _req.target(_target); _req.version(11);
        _req.set(http::field::host,          _host);
        _req.set(http::field::content_type,  "application/json");
        _req.set(http::field::authorization, "Bearer " + _api_key);
        _req.set(http::field::user_agent,    "WeixinChat/1.0");
        _req.set(http::field::connection,    "close");
        _req.body() = _body; _req.prepare_payload();
        _resolver.async_resolve(_host, _port,
            beast::bind_front_handler(&AIHttpSession::OnResolve, shared_from_this()));
    }
private:
    void OnResolve(beast::error_code ec, tcp::resolver::results_type r) {
        if (ec) return Fail("resolve",ec);
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(15));
        beast::get_lowest_layer(_stream).async_connect(r,
            beast::bind_front_handler(&AIHttpSession::OnConnect, shared_from_this()));
    }
    void OnConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
        if (ec) return Fail("connect",ec);
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(15));
        _stream.async_handshake(ssl::stream_base::client,
            beast::bind_front_handler(&AIHttpSession::OnHandshake, shared_from_this()));
    }
    void OnHandshake(beast::error_code ec) {
        if (ec) return Fail("handshake",ec);
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(30));
        http::async_write(_stream, _req,
            beast::bind_front_handler(&AIHttpSession::OnWrite, shared_from_this()));
    }
    void OnWrite(beast::error_code ec, std::size_t) {
        if (ec) return Fail("write",ec);
        http::async_read(_stream, _buffer, _res,
            beast::bind_front_handler(&AIHttpSession::OnRead, shared_from_this()));
    }
    void OnRead(beast::error_code ec, std::size_t) {
        if (ec) return Fail("read",ec);
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(5));
        _stream.async_shutdown(
            beast::bind_front_handler(&AIHttpSession::OnShutdown, shared_from_this()));
    }
    void OnShutdown(beast::error_code) { HandleResponse(); }
    void HandleResponse() {
        if (_res.result_int() != 200) {
            std::cerr << "[P1] HTTP " << _res.result_int() << "\n";
            Fallback("AI request failed (HTTP " + std::to_string(_res.result_int()) + ")"); return;
        }
        Json::Value root; Json::Reader rdr;
        if (!rdr.parse(_res.body(), root)) { Fallback("AI request failed"); return; }
        try {
            const Json::Value& msg    = root["choices"][0]["message"];
            const std::string  finish = root["choices"][0]["finish_reason"].asString();
            const bool is_tc = (finish=="tool_calls") ||
                (msg.isMember("tool_calls") && msg["tool_calls"].isArray() && !msg["tool_calls"].empty());
            if (is_tc && _on_tool_call) {
                _on_tool_call(msg); return;
            }

            const std::string ans = msg.isMember("content") && msg["content"].isString()
                ? msg["content"].asString() : "";

            // 兼容模型返回的“文本型 tool_call”格式（非标准 tool_calls JSON）
            if (_on_tool_call && ans.find("tool_call") != std::string::npos) {
                auto extract_string = [](const std::string& src, const std::string& key) {
                    const std::string k1 = "\"" + key + "\"";
                    auto pos = src.find(k1);
                    if (pos == std::string::npos) return std::string();
                    pos = src.find(':', pos);
                    if (pos == std::string::npos) return std::string();
                    auto q1 = src.find('"', pos + 1);
                    if (q1 == std::string::npos) return std::string();
                    auto q2 = src.find('"', q1 + 1);
                    if (q2 == std::string::npos) return std::string();
                    return src.substr(q1 + 1, q2 - q1 - 1);
                };
                auto extract_int = [](const std::string& src, const std::string& key) {
                    const std::string k1 = "\"" + key + "\"";
                    auto pos = src.find(k1);
                    if (pos == std::string::npos) return 0;
                    pos = src.find(':', pos);
                    if (pos == std::string::npos) return 0;
                    std::size_t start = src.find_first_of("-0123456789", pos + 1);
                    if (start == std::string::npos) return 0;
                    std::size_t end = src.find_first_not_of("0123456789", start);
                    return std::stoi(src.substr(start, end - start));
                };

                std::string name = extract_string(ans, "name");
                if (!name.empty()) {
                    Json::Value args(Json::objectValue);
                    const std::string name_kw = extract_string(ans, "name_keyword");
                    const std::string keyword = extract_string(ans, "keyword");
                    const int limit = extract_int(ans, "limit");
                    const int uid2 = extract_int(ans, "uid2");
                    const int target_uid = extract_int(ans, "target_uid");
                    if (!name_kw.empty()) args["name_keyword"] = name_kw;
                    if (!keyword.empty()) args["keyword"] = keyword;
                    if (limit > 0) args["limit"] = limit;
                    if (uid2 > 0) args["uid2"] = uid2;
                    if (target_uid > 0) args["target_uid"] = target_uid;

                    Json::FastWriter fw;
                    std::string args_str = fw.write(args);
                    if (!args_str.empty() && args_str.back() == '\n') args_str.pop_back();

                    Json::Value fake_msg;
                    Json::Value tc_arr(Json::arrayValue);
                    Json::Value tc;
                    tc["id"] = "toolcall_text_1";
                    tc["type"] = "function";
                    tc["function"]["name"] = name;
                    tc["function"]["arguments"] = args_str;
                    tc_arr.append(tc);
                    fake_msg["tool_calls"] = tc_arr;
                    std::cout << "[P1] parsed text tool_call name=" << name
                              << " args=" << args_str << "\n";
                    _on_tool_call(fake_msg);
                    return;
                }
            }

            if (ans.empty()) throw std::runtime_error("empty");
            if (_on_answer) _on_answer(ans);
            Fallback(ans);
        } catch (const std::exception& e) {
            std::cerr << "[P1] parse: " << e.what() << "\n"; Fallback("AI request failed");
        }
    }
    void Fallback(const std::string& text) {
        const std::string us = std::to_string(_from_uid);
        auto s = SessionManager::GetInstance()->GetSession(us);
        if (!s) {
            // 用户离线：fire-and-forget 写库，不在网络线程做同步 MySQL
            int uid = _from_uid;
            DbWriteQueue::GetInstance()->PostWrite([uid, text]() {
                try {
                    auto db = MysqlConPool::GetInstance()->GetSession();
                    if (!db) return;
                    MysqlObj o(std::move(db));
                    o.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
                     .getTable("offline_messages")
                     .insert("id","from_uid","to_uid","content","type")
                     .values(NewSnowflakeID(), AgentManager::BOT_UID, uid, text, 0).execute();
                } catch (...) {}
            });
            return;
        }
        const int64_t bt = static_cast<int64_t>(::time(nullptr));
        static std::atomic<uint64_t> sq{1};
        Json::Value m;
        m["msg_id"]    = "9999_"+us+"_"+std::to_string(bt)+"_"+std::to_string(sq.fetch_add(1));
        m["from_uid"]  = AgentManager::BOT_UID;
        m["content"]   = text;
        m["timestamp"] = static_cast<Json::Int64>(::time(nullptr));
        Json::FastWriter fw; auto js = fw.write(m);
        if (!js.empty() && js.back()=='\n') js.pop_back();
        s->Send(ID_AI_CHAT_MSG, js);
    }
    void Fail(const std::string& w, beast::error_code ec) {
        std::cerr << "[P1] " << w << ": " << ec.message() << "\n"; Fallback("AI request failed");
    }
    tcp::resolver _resolver; beast::ssl_stream<beast::tcp_stream> _stream;
    beast::flat_buffer _buffer; http::request<http::string_body> _req;
    http::response<http::string_body> _res;
    int _from_uid; std::string _host,_port,_target,_body,_api_key;
    AnswerCallback _on_answer; ToolCallCallback _on_tool_call;
};

// ============================================================
// AIStreamSession  (Pass-2  stream=true)
//
// Reads SSE response incrementally via http::response_parser<http::buffer_body>
// + async_read_some loop.
//
// TCP stream protocol (all frames share the same msg_id):
//   stream_status="start"     first content chunk (+ timestamp)
//   stream_status="streaming" each delta piece    (+ content)
//   stream_status="end"       on [DONE]
//
// Offline fallback: if user is absent, accumulate and save to offline_messages.
// ============================================================
class AIStreamSession : public std::enable_shared_from_this<AIStreamSession> {
public:
    using DoneCallback = std::function<void(const std::string& full_text)>;

    AIStreamSession(net::io_context& ioc, ssl::context& ctx, int uid,
                    std::string host, std::string port, std::string target,
                    std::string body, std::string key,
                    std::string msg_id, DoneCallback done)
        : _resolver(net::make_strand(ioc)), _stream(net::make_strand(ioc), ctx)
        , _from_uid(uid), _host(std::move(host)), _port(std::move(port))
        , _target(std::move(target)), _body(std::move(body)), _api_key(std::move(key))
        , _stream_msg_id(std::move(msg_id)), _on_done(std::move(done))
        , _stream_started(false), _sse_done(false)
        , _user_online(true), _done_processed(false)
        , _stream_seq(0)
    {
        _parser.body_limit(std::numeric_limits<std::uint64_t>::max());
    }

    void Start() {
        if (!SSL_set_tlsext_host_name(_stream.native_handle(), _host.c_str())) return;
        _req.method(http::verb::post); _req.target(_target); _req.version(11);
        _req.set(http::field::host,          _host);
        _req.set(http::field::content_type,  "application/json");
        _req.set(http::field::authorization, "Bearer " + _api_key);
        _req.set(http::field::user_agent,    "WeixinChat/1.0");
        _req.set(http::field::accept,        "text/event-stream");
        _req.set(http::field::connection,    "close");
        _req.body() = _body; _req.prepare_payload();
        std::cout << "[Agent][Stream] start uid=" << _from_uid
                  << " host=" << _host << " target=" << _target << "\n";
        _resolver.async_resolve(_host, _port,
            beast::bind_front_handler(&AIStreamSession::OnResolve, shared_from_this()));
    }

private:
    void OnResolve(beast::error_code ec, tcp::resolver::results_type r) {
        if (ec) return Fail("resolve",ec);
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(15));
        beast::get_lowest_layer(_stream).async_connect(r,
            beast::bind_front_handler(&AIStreamSession::OnConnect, shared_from_this()));
    }
    void OnConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
        if (ec) return Fail("connect",ec);
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(15));
        _stream.async_handshake(ssl::stream_base::client,
            beast::bind_front_handler(&AIStreamSession::OnHandshake, shared_from_this()));
    }
    void OnHandshake(beast::error_code ec) {
        if (ec) return Fail("handshake",ec);
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(30));
        http::async_write(_stream, _req,
            beast::bind_front_handler(&AIStreamSession::OnWrite, shared_from_this()));
    }
    // After write: read only HTTP headers, not the body yet
    void OnWrite(beast::error_code ec, std::size_t) {
        if (ec) return Fail("write",ec);
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(30));
        http::async_read_header(_stream, _flat_buf, _parser,
            beast::bind_front_handler(&AIStreamSession::OnHeader, shared_from_this()));
    }
    void OnHeader(beast::error_code ec, std::size_t) {
        if (ec) return Fail("read_header",ec);
        std::cout << "[Agent][Stream] header HTTP=" << _parser.get().result_int() << "\n";
        if (_parser.get().result_int() != 200) {
            FinishStream("AI request failed (HTTP " + std::to_string(_parser.get().result_int()) + ")");
            return;
        }
        DoReadBody();
    }

    // ── Incremental body reading loop ────────────────────────────────────
    void DoReadBody() {
        _parser.get().body().data = _read_buf.data();
        _parser.get().body().size = _read_buf.size();
        _parser.get().body().more = true;
        // Allow extra time during streaming (LLM may be slow)
        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(90));
        http::async_read_some(_stream, _flat_buf, _parser,
            beast::bind_front_handler(&AIStreamSession::OnBodyChunk, shared_from_this()));
    }

    void OnBodyChunk(beast::error_code ec, std::size_t /*xfer*/) {
        // Bytes written = initial capacity - remaining capacity
        const std::size_t n = _read_buf.size() - _parser.get().body().size;
        if (n > 0) {
            _sse_buf.append(_read_buf.data(), n);
            ParseSSEBuffer();
        }

        if (_sse_done)          { std::cout << "[Agent][Stream] done via [DONE]\n"; FinishStream(""); return; } // [DONE] received
        if (_parser.is_done())  { std::cout << "[Agent][Stream] done via parser\n"; FinishStream(""); return; } // parser says done
        if (ec == http::error::need_buffer) { DoReadBody(); return; } // refill buffer
        if (!ec)                { DoReadBody(); return; } // more data coming

        // ec is set and not need_buffer
        // After [DONE] the server closes the connection → normal EOF
        FinishStream(_sse_done ? "" : "AI request failed");
    }

    // ── SSE parsing ──────────────────────────────────────────────────────

    void ParseSSEBuffer() {
        std::size_t pos;
        while ((pos = _sse_buf.find("\n\n")) != std::string::npos) {
            std::string ev = _sse_buf.substr(0, pos);
            _sse_buf.erase(0, pos + 2);
            ProcessSSEEvent(ev);
        }
    }

    void ProcessSSEEvent(const std::string& event) {
        std::size_t ls = 0;
        while (ls < event.size()) {
            std::size_t le = event.find('\n', ls);
            if (le == std::string::npos) le = event.size();
            std::string line = event.substr(ls, le - ls);
            if (!line.empty() && line.back()=='\r') line.pop_back();

            const std::string pfx = "data: ";
            if (line.rfind(pfx, 0) == 0) {
                std::string data = line.substr(pfx.size());
                if (data == "[DONE]") { _sse_done = true; return; }

                Json::Value chunk; Json::Reader rdr;
                if (!rdr.parse(data, chunk)) { ls = le+1; continue; }
                try {
                    const Json::Value& delta = chunk["choices"][0]["delta"];
                    if (delta.isMember("content") && delta["content"].isString()) {
                        const std::string piece = delta["content"].asString();
                        if (!piece.empty()) {
                            _full_reply += piece;
                            PushChunk(piece);
                        }
                    }
                } catch (...) {}
            }
            ls = le + 1;
        }
    }

    // ── TCP push ─────────────────────────────────────────────────────────

    void PushChunk(const std::string& piece) {
        const std::string us = std::to_string(_from_uid);
        auto sess = SessionManager::GetInstance()->GetSession(us);
        if (!sess) { _user_online = false; return; }
        if (!_user_online) return;
        std::cout << "[Agent][Stream] chunk size=" << piece.size() << "\n";

        Json::FastWriter fw;

        if (!_stream_started) {
            // "start" frame: unique msg_id, shared stream_id for client bubble routing
            Json::Value sm;
            sm["msg_id"]        = _stream_msg_id + "_s" + std::to_string(_stream_seq++);
            sm["stream_id"]     = _stream_msg_id;
            sm["from_uid"]      = AgentManager::BOT_UID;
            sm["stream_status"] = "start";
            sm["timestamp"]     = static_cast<Json::Int64>(::time(nullptr));
            auto s = fw.write(sm); if (!s.empty()&&s.back()=='\n') s.pop_back();
            sess->Send(ID_AI_CHAT_MSG, s);
            _stream_started = true;
        }

        // "streaming" frame: every piece gets its own unique msg_id
        Json::Value m;
        m["msg_id"]        = _stream_msg_id + "_s" + std::to_string(_stream_seq++);
        m["stream_id"]     = _stream_msg_id;
        m["from_uid"]      = AgentManager::BOT_UID;
        m["stream_status"] = "streaming";
        m["content"]       = piece;
        auto s = fw.write(m); if (!s.empty()&&s.back()=='\n') s.pop_back();
        sess->Send(ID_AI_CHAT_MSG, s);
    }

    void PushEnd() {
        if (!_user_online) return;
        const std::string us = std::to_string(_from_uid);
        auto sess = SessionManager::GetInstance()->GetSession(us);
        if (!sess) return;
        // "end" frame: unique msg_id so dedup guard lets it through
        Json::Value m;
        m["msg_id"]        = _stream_msg_id + "_end";
        m["stream_id"]     = _stream_msg_id;
        m["from_uid"]      = AgentManager::BOT_UID;
        m["stream_status"] = "end";
        Json::FastWriter fw; auto s = fw.write(m);
        if (!s.empty()&&s.back()=='\n') s.pop_back();
        sess->Send(ID_AI_CHAT_MSG, s);
    }

    // ── Stream completion ─────────────────────────────────────────────────

    void FinishStream(const std::string& err_override) {
        if (_done_processed) return;
        _done_processed = true;

        const std::string final_text = err_override.empty() ? _full_reply : err_override;

        if (!err_override.empty()) {
            Fallback(final_text);   // error path: plain message frame
        } else if (_user_online) {
            PushEnd();
        } else {
            SaveOffline(final_text);
        }

        if (!final_text.empty() && _on_done) _on_done(final_text);

        beast::get_lowest_layer(_stream).expires_after(std::chrono::seconds(3));
        _stream.async_shutdown(
            beast::bind_front_handler(&AIStreamSession::OnShutdown, shared_from_this()));
    }

    void OnShutdown(beast::error_code) { /* graceful, ignore errors */ }

    void SaveOffline(const std::string& text) {
        if (text.empty()) return;
        // 不在网络线程做同步 MySQL，fire-and-forget 到写队列
        int uid = _from_uid;
        DbWriteQueue::GetInstance()->PostWrite([uid, text]() {
            try {
                auto db = MysqlConPool::GetInstance()->GetSession();
                if (!db) return;
                MysqlObj o(std::move(db));
                o.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
                 .getTable("offline_messages")
                 .insert("id","from_uid","to_uid","content","type")
                 .values(NewSnowflakeID(), AgentManager::BOT_UID, uid, text, 0).execute();
            } catch (const std::exception& e) {
                std::cerr << "[Agent][Stream] SaveOffline: " << e.what() << "\n";
            }
        });
    }

    void Fallback(const std::string& text) {
        const std::string us = std::to_string(_from_uid);
        auto sess = SessionManager::GetInstance()->GetSession(us);
        if (!sess) { SaveOffline(text); return; }
        const int64_t bt = static_cast<int64_t>(::time(nullptr));
        static std::atomic<uint64_t> sq{1};
        Json::Value m;
        m["msg_id"]    = "9999_"+us+"_"+std::to_string(bt)+"_"+std::to_string(sq.fetch_add(1));
        m["from_uid"]  = AgentManager::BOT_UID;
        m["content"]   = text;
        m["timestamp"] = static_cast<Json::Int64>(::time(nullptr));
        Json::FastWriter fw; auto s = fw.write(m);
        if (!s.empty()&&s.back()=='\n') s.pop_back();
        sess->Send(ID_AI_CHAT_MSG, s);
    }

    void Fail(const std::string& w, beast::error_code ec) {
        std::cerr << "[Agent][Stream] " << w << ": " << ec.message() << "\n";
        FinishStream("AI request failed");
    }

    tcp::resolver _resolver; beast::ssl_stream<beast::tcp_stream> _stream;
    beast::flat_buffer _flat_buf; http::request<http::string_body> _req;
    http::response_parser<http::buffer_body> _parser;
    std::array<char,8192> _read_buf;

    int _from_uid; std::string _host,_port,_target,_body,_api_key;
    std::string _stream_msg_id; DoneCallback _on_done;

    std::string  _sse_buf;        // partial SSE event buffer
    std::string  _full_reply;     // accumulated full text (written to pool on [DONE])
    bool         _stream_started;
    bool         _sse_done;
    bool         _user_online;
    bool         _done_processed;
    uint32_t     _stream_seq;     // per-stream frame counter; makes every msg_id unique
};

// ============================================================
// AgentManager  constructor
// ============================================================
AgentManager::AgentManager()
{
    auto& cfg   = ConfigMgr::Inst()["AI_Agent"];
    _api_key    = cfg["ApiKey"];
    _model_name = cfg["ModelName"];
    std::string url = cfg["ApiUrl"];
    const std::string prefix = "https://";
    if (url.rfind(prefix,0)==0) url = url.substr(prefix.size());
    const auto sp = url.find('/');
    if (sp != std::string::npos) {
        _host   = url.substr(0, sp);
        _target = url.substr(sp) + "/chat/completions";
    } else { _host = url; _target = "/chat/completions"; }
    _port = "443";
    _ssl_ctx = std::make_shared<ssl::context>(ssl::context::tls_client);
    _ssl_ctx->set_default_verify_paths();
    _ssl_ctx->set_verify_mode(ssl::verify_peer);
    std::cout << "[Agent] host=" << _host << "  model=" << _model_name << "\n";
}

// ============================================================
// Local tools
// ============================================================

/*static*/ std::string AgentManager::get_server_time()
{
    std::time_t t = std::time(nullptr); std::tm tv{};
#ifdef _WIN32
    localtime_s(&tv, &t);
#else
    localtime_r(&t, &tv);
#endif
    char buf[32]; std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tv);
    return buf;
}

/*static*/ std::string AgentManager::query_friend_info(int caller_uid,
                                                        const std::string& name_keyword,
                                                        int limit)
{
    // 参数防御
    if (caller_uid <= 0) {
        return R"({"found":false,"users":[],"error":"invalid caller"})";
    }
    if (name_keyword.empty() || name_keyword.size() > 20) {
        return R"({"found":false,"users":[],"error":"invalid keyword"})";
    }
    if (limit <= 0 || limit > 10) limit = 5;

    // 只在调用者的好友列表中搜索：
    //   - 非好友对结果不可见（隐私保护 + 越权防御）
    //   - 同名好友会全部返回并附上 uid，AI 可引导用户通过 uid 确认目标
    std::vector<UserInfo> users;
    MysqlDao dao;
    bool ok = dao.SearchFriendsByName(caller_uid, name_keyword, limit, users);

    Json::Value root;
    root["found"] = ok && !users.empty();
    Json::Value arr(Json::arrayValue);
    for (const auto& u : users) {
        Json::Value item;
        item["uid"]   = u.uid;
        item["name"]  = u.name;
        item["email"] = u.email;
        arr.append(item);
    }
    root["users"] = arr;
    // 若出现同名，提示 AI 应向用户澄清目标
    if (users.size() > 1) {
        root["hint"] = "multiple users found with similar name, "
                       "please clarify which one by uid";
    }
    Json::FastWriter fw;
    auto s = fw.write(root);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

/*static*/ std::string AgentManager::get_online_status(int caller_uid, int target_uid)
{
    if (caller_uid <= 0 || target_uid <= 0) {
        return R"({"uid":0,"online":false,"error":"invalid uid"})";
    }

    // 好友鉴权：只允许查询好友的在线状态，陌生人一律拒绝
    // BOT_UID 例外——任何人都可以查机器人是否"在线"
    if (target_uid != BOT_UID) {
        MysqlDao dao_check;
        if (!dao_check.AreFriends(caller_uid, target_uid)) {
            Json::Value err;
            err["uid"]    = target_uid;
            err["online"] = false;
            err["error"]  = "not_friend";
            Json::FastWriter fw; auto s = fw.write(err);
            if (!s.empty() && s.back() == '\n') s.pop_back();
            return s;
        }
    }

    const std::string uid_str = std::to_string(target_uid);

    // 1. 先查本机 SessionManager（O(1)，最快路径）
    bool online = SessionManager::GetInstance()->IsUserOnline(uid_str);

    // 2. 若本机没有，查 Redis（uid_server:<uid> key 存在说明在其他 ChatServer）
    std::string note;
    bool route_known = false;
    if (!online) {
        std::string server_addr;
        try {
            route_known = RedisMgr::GetInstance()->Get("uid_server:" + uid_str, server_addr);
            if (route_known) {
                note = "route found in redis, but live online status is not verified";
            } else {
                note = "offline";
            }
        } catch (...) {
            note = "redis unavailable, online status not verified";
        }
    } else {
        note = "online on this server";
    }

    // 3. 查询用户名，让 AI 回复时可以说出名字而不是裸 uid
    UserInfo info;
    MysqlDao dao;
    dao.GetUserByUid(target_uid, info);

    Json::Value root;
    root["uid"]    = target_uid;
    root["name"]   = info.name.empty() ? ("uid_" + uid_str) : info.name;
    root["online"] = online;
    root["route_known"] = route_known;
    root["verified_live"] = online;
    if (!note.empty()) root["note"] = note;

    Json::FastWriter fw;
    auto s = fw.write(root);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

/*static*/ std::string AgentManager::search_chat_history(int caller_uid,
                                                          int uid2,
                                                          const std::string& friend_name,
                                                          const std::string& keyword,
                                                          int limit)
{
    auto build_error = [](const std::string& error_code,
                          const Json::Value* extra = nullptr) {
        Json::Value err;
        err["found"] = false;
        err["count"] = 0;
        err["records"] = Json::Value(Json::arrayValue);
        err["error"] = error_code;
        if (extra) {
            for (const auto& key : extra->getMemberNames()) {
                err[key] = (*extra)[key];
            }
        }
        Json::FastWriter fw;
        auto s = fw.write(err);
        if (!s.empty() && s.back() == '\n') s.pop_back();
        return s;
    };

    if (caller_uid <= 0) {
        return build_error("invalid_caller");
    }
    if (limit <= 0 || limit > 20) limit = 5;

    std::string eff_keyword = keyword;
    int peer = (uid2 == 0) ? BOT_UID : uid2;

    // 若 uid2 未指定且给了关键词，尝试把关键词当作好友名解析
    auto resolve_peer = [&](const std::string& raw_text,
                            bool clear_keyword_on_success) -> std::string {
        if (raw_text.empty()) {
            return std::string();
        }

        MysqlDao dao_find;
        std::vector<UserInfo> candidates;
        if (dao_find.SearchFriendsByName(caller_uid, raw_text, 10, candidates)) {
            if (candidates.size() == 1) {
                peer = candidates.front().uid;
                if (clear_keyword_on_success) {
                    eff_keyword.clear();
                }
                return std::string();
            }
            if (candidates.size() > 1) {
                Json::Value extra;
                Json::Value arr(Json::arrayValue);
                for (const auto& c : candidates) {
                    Json::Value item;
                    item["uid"] = c.uid;
                    item["name"] = c.name;
                    arr.append(item);
                }
                extra["candidates"] = arr;
                return build_error("ambiguous_friend", &extra);
            }
        }

        std::vector<UserInfo> friends;
        if (!dao_find.GetFriendsByUid(caller_uid, friends)) {
            return std::string();
        }

        std::vector<UserInfo> fuzzy;
        for (const auto& f : friends) {
            if (f.name.empty()) {
                continue;
            }
            if (raw_text.find(f.name) != std::string::npos ||
                f.name.find(raw_text) != std::string::npos) {
                fuzzy.push_back(f);
            }
        }

        if (fuzzy.size() == 1) {
            peer = fuzzy.front().uid;
            if (clear_keyword_on_success) {
                eff_keyword.clear();
            }
            return std::string();
        }
        if (fuzzy.size() > 1) {
            Json::Value extra;
            Json::Value arr(Json::arrayValue);
            for (const auto& c : fuzzy) {
                Json::Value item;
                item["uid"] = c.uid;
                item["name"] = c.name;
                arr.append(item);
            }
            extra["candidates"] = arr;
            return build_error("ambiguous_friend", &extra);
        }

        return std::string();
    };

    if (peer <= 0 && !friend_name.empty()) {
        if (auto err = resolve_peer(friend_name, false); !err.empty()) {
            return err;
        }
    }
    if (peer <= 0 && !keyword.empty()) {
        if (auto err = resolve_peer(keyword, true); !err.empty()) {
            return err;
        }
    }
    if (peer <= 0 && friend_name.empty() && keyword.empty()) {
        return build_error("missing_target_uid");
    }
    if (peer <= 0) {
        return build_error("unresolved_friend");
    }

    // uid2 == 0 => 查与机器人的对话，天然合法
    // uid2 == caller_uid => 查自己发给自己（合法，某些客户端支持收藏）
    // 其他情况 => 必须是好友才能查
    if (peer != BOT_UID && peer != caller_uid) {
        MysqlDao dao_check;
        if (!dao_check.AreFriends(caller_uid, peer)) {
            return build_error("not_friend");
        }
    }

    {
        auto* self = AgentManager::GetInstance();
        std::lock_guard<std::mutex> lk(self->_pool_mutex);
        self->_last_history_peer[caller_uid] = peer;
    }

    std::vector<MysqlDao::ChatRecord> records;
    MysqlDao dao;
    std::cout << "[Agent][Tool] search_chat_history caller=" << caller_uid
              << " peer=" << peer
              << " keyword=\"" << eff_keyword << "\""
              << " limit=" << limit << std::endl;
    if (!dao.SearchChatHistory(caller_uid, peer, eff_keyword, limit, records)) {
        return build_error("db_error");
    }
    bool ignored_keyword = false;
    if (records.empty() && !eff_keyword.empty()) {
        std::cout << "[Agent][Tool] search_chat_history no match with keyword, retry without keyword" << std::endl;
        if (!dao.SearchChatHistory(caller_uid, peer, std::string(), limit, records)) {
            return build_error("db_error");
        }
        ignored_keyword = !records.empty();
    }

    Json::Value root;
    root["found"] = !records.empty();
    root["count"] = static_cast<int>(records.size());
    root["peer_uid"] = peer;
    if (ignored_keyword) {
        root["keyword_ignored"] = true;
    }
    if (!records.empty()) {
        const auto& latest = records.back();
        Json::Value latest_item;
        latest_item["from"] = latest.from_uid;
        latest_item["to"] = latest.to_uid;
        latest_item["content"] = latest.content;
        latest_item["time"] = latest.created_at;
        root["latest_record"] = latest_item;
    }
    Json::Value arr(Json::arrayValue);
    for (const auto& r : records) {
        Json::Value item;
        item["from"]    = r.from_uid;
        item["to"]      = r.to_uid;
        item["content"] = r.content;
        item["time"]    = r.created_at;
        arr.append(item);
    }
    root["records"] = arr;

    Json::FastWriter fw;
    auto s = fw.write(root);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// _dispatchTool：统一入口，caller_uid 是当前对话用户的 uid，传入每个工具做权限校验
/*static*/ std::string AgentManager::_dispatchTool(const std::string& name,
                                                    const Json::Value& args,
                                                    int caller_uid)
{
    if (name == "get_server_time") {
        return AgentManager::get_server_time();
    }

    if (name == "query_friend_info") {
        const std::string keyword = args.get("name_keyword", "").asString();
        const int         limit   = args.get("limit", 5).asInt();
        // caller_uid 作为好友搜索范围的锚点，LLM 传来的 name_keyword 即可
        return AgentManager::query_friend_info(caller_uid, keyword, limit);
    }

    if (name == "get_online_status") {
        const int target_uid = args.get("target_uid", 0).asInt();
        return AgentManager::get_online_status(caller_uid, target_uid);
    }

    if (name == "search_chat_history") {
        // uid1 固定为 caller_uid（LLM 不应伪造发起方）
        const int uid2          = args.isMember("uid2") ? args["uid2"].asInt() : -1;
        const std::string friend_name = args.get("friend_name", "").asString();
        const std::string keyword = args.get("keyword", "").asString();
        const int limit         = args.get("limit", 5).asInt();
        return AgentManager::search_chat_history(caller_uid, uid2, friend_name, keyword, limit);
    }

    return "[error: unknown tool \"" + name + "\"]";
}

// ============================================================
// Memory pool helpers  (caller holds _pool_mutex)
// ============================================================
void AgentManager::_appendMessage(int uid, const std::string& role, const std::string& content)
{
    AgentMessage m; m.role = role; m.content = content;
    auto& h = _memory_pool[uid]; h.push_back(std::move(m));
    while (static_cast<int>(h.size()) > MAX_HISTORY_SIZE) h.pop_front();
}
void AgentManager::_appendToolCallMsg(int uid, const std::string& tc, const std::string& content)
{
    AgentMessage m; m.role = "assistant"; m.content = content; m.tool_calls_json = tc;
    auto& h = _memory_pool[uid]; h.push_back(std::move(m));
    while (static_cast<int>(h.size()) > MAX_HISTORY_SIZE) h.pop_front();
}
void AgentManager::_appendToolResultMsg(int uid, const std::string& tcid, const std::string& result)
{
    AgentMessage m; m.role = "tool"; m.content = result; m.tool_call_id = tcid;
    auto& h = _memory_pool[uid]; h.push_back(std::move(m));
    while (static_cast<int>(h.size()) > MAX_HISTORY_SIZE) h.pop_front();
}
void AgentManager::clearMemory(int uid)
{
    std::lock_guard<std::mutex> lk(_pool_mutex);
    _memory_pool.erase(uid);
    _last_history_peer.erase(uid);
    _runtime.Clear(uid);
}

// ============================================================
// _saveBotReplyToDb  ──  将机器人回复小存 chat_messages，供下次重启后恢复记忆
// ============================================================
/*static*/ void AgentManager::_saveBotReplyToDb(int to_uid, const std::string& text)
{
    if (text.empty()) return;
    try {
        auto db = MysqlConPool::GetInstance()->GetSession();
        if (!db) return;
        MysqlObj o(std::move(db));
        o.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
         .getTable("chat_messages")
         .insert("id","from_uid","to_uid","content")
         .values(NewSnowflakeID(), AgentManager::BOT_UID, to_uid, text).execute();
    } catch (const std::exception& e) {
        std::cerr << "[Agent] _saveBotReplyToDb: " << e.what() << "\n";
    }
}

// ============================================================
// _loadHistoryFromDb  ──  服务器重启后首次对话时从 DB 恢复记忆池
// 调用前必须持有 _pool_mutex
// ============================================================
void AgentManager::_loadHistoryFromDb(int uid)
{
    // 已经加载过（即使是空历史），用 sentinel 小 trick：如果 key 已存在就不重复加载
    if (_memory_pool.count(uid)) return;
    // 插入一个空列表表示 “已初始化过”
    _memory_pool[uid];
    try {
        auto db = MysqlConPool::GetInstance()->GetSession();
        if (!db) return;
        MysqlObj o(std::move(db));
        auto tbl = o.get()->getSchema(MysqlConPool::GetInstance()->GetSchema())
                           .getTable("chat_messages");
        // 取最新的 MAX_HISTORY_SIZE 条（按时间倒序，稍后反转）
        // 注意：MySQL X DevAPI 要求 orderBy/limit 在 bind 之前调用
        auto res = tbl.select("from_uid","content")
                      .where("(from_uid = :uid AND to_uid = :bot) OR "
                             "(from_uid = :bot AND to_uid = :uid)")
                      .orderBy("created_at DESC")
                      .limit(MAX_HISTORY_SIZE)
                      .bind("uid", uid)
                      .bind("bot", AgentManager::BOT_UID)
                      .execute();
        // 收集到 vector 再反转，保证时间正序
        std::vector<std::pair<int,std::string>> rows;
        for (auto row = res.fetchOne(); !row.isNull(); row = res.fetchOne()) {
            int  fuid    = static_cast<int>(row[0]);
            std::string content = row[1].get<std::string>();
            rows.push_back({fuid, std::move(content)});
        }
        std::reverse(rows.begin(), rows.end());
        auto& hist = _memory_pool[uid];
        for (auto& [fuid, content] : rows) {
            AgentMessage m;
            m.role    = (fuid == uid) ? "user" : "assistant";
            m.content = std::move(content);
            hist.push_back(std::move(m));
        }
        std::cout << "[Agent] Loaded " << hist.size()
                  << " history messages for uid=" << uid << "\n";    } catch (const std::exception& e) {
        std::cerr << "[Agent] _loadHistoryFromDb: " << e.what() << "\n";
    }
}

// ============================================================
// _buildMessagesArray  (caller holds _pool_mutex)
// ============================================================
Json::Value AgentManager::_buildMessagesArray(int uid) const
{
    Json::Value arr(Json::arrayValue);
    Json::Value sys; sys["role"]="system";
    sys["content"]="你是本软件的专属AI助手，说话自然幽默，请结合上下文回答用户的问题，控制在150字以内。如有需要可使用提供的工具。"
        " You are the in-app AI assistant. Be concise and factual."
        " Do not claim a message was sent unless the system explicitly confirms it."
        " Do not claim a user is online unless a tool result explicitly returns online=true."
        " Do not infer that both sides are online from a single tool result."
        " When quoting times, use exact values from tool results only."
        " If the user asks you to send or reply to a friend, do not say it was already sent;"
        " draft the message only unless the system later confirms delivery.";
    arr.append(sys);

    Json::Value guidance;
    guidance["role"] = "system";
    guidance["content"] =
        "回答要简洁，优先结合当前任务和最近上下文。"
        " Prefer exact tool output over interpretation. If certainty is missing, say so.";
    arr.append(guidance);

    AgentTask task;
    if (_runtime.TryGetTask(uid, task) && task.pending_action == kActionDraftSendText) {
        Json::Value draft_sys;
        draft_sys["role"] = "system";
        draft_sys["content"] =
            "当前任务是为用户起草一条准备发给好友的消息。"
            "如果信息已经足够，请只输出“草稿：”加可直接发送的正文，不要解释、不要加引号、不要附加分析。"
            "如果信息还不够，请直接向用户提一个简短澄清问题，不要输出“草稿：”。"
            " Current task: draft one message the user may send to a friend."
            " If context is sufficient, output only 'Draft:' followed by the final message body."
            " Do not mention online status, delivery status, read status, or server time."
            " If context is insufficient, ask one short clarification question and do not output 'Draft:'.";
        arr.append(draft_sys);
    }
    const auto it = _memory_pool.find(uid);
    if (it == _memory_pool.end()) return arr;
    for (const auto& m : it->second) {
        Json::Value item; item["role"] = m.role;
        if (!m.tool_calls_json.empty()) {
            item["content"] = Json::Value(Json::nullValue);
            Json::Value tca; Json::Reader r;
            if (r.parse(m.tool_calls_json, tca)) item["tool_calls"] = tca;
            else item["content"] = m.tool_calls_json;
        } else if (!m.tool_call_id.empty()) {
            item["content"] = m.content; item["tool_call_id"] = m.tool_call_id;
        } else {
            item["content"] = m.content;
        }
        arr.append(item);
    }
    return arr;
}

// ============================================================
// _buildToolsArray  (file-scope helper)
// 所有工具的 OpenAI Function Calling JSON Schema 在此统一注册。
// 新增工具只需追加一个 Json::Value，无需改其他地方。
// ============================================================
static Json::Value _buildToolsArray()
{
    Json::Value tools(Json::arrayValue);

    // ── 工具 1：get_server_time ──────────────────────────────────────────
    {
        Json::Value fn;
        fn["name"] = "get_server_time";
        fn["description"] =
            "获取服务器当前时间，返回 'YYYY-MM-DD HH:MM:SS' 字符串。"
            "【仅在用户明确询问时间/日期时调用，闲聊时禁止调用。】";
        Json::Value params;
        params["type"]       = "object";
        params["properties"] = Json::Value(Json::objectValue);
        params["required"]   = Json::Value(Json::arrayValue);
        fn["parameters"] = params;

        Json::Value tool;
        tool["type"]     = "function";
        tool["function"] = fn;
        tools.append(tool);
    }

    // ── 工具 2：query_friend_info ────────────────────────────────────────
    {
        Json::Value fn;
        fn["name"] = "query_friend_info";
        fn["description"] =
            "按昵称关键词搜索系统中的用户，返回匹配用户的 uid、昵称和邮箱。"
            "【仅在用户明确提到要查询某人信息、或询问\"xxx 的 uid/邮箱是多少\"时调用。】";

        Json::Value props;
        Json::Value kw;
        kw["type"]        = "string";
        kw["description"] = "要搜索的昵称关键词，支持模糊匹配，长度 1-20 个字符";
        props["name_keyword"] = kw;

        Json::Value lm;
        lm["type"]        = "integer";
        lm["description"] = "最多返回的结果数量，默认 5，最大 10";
        lm["default"]     = 5;
        lm["minimum"]     = 1;
        lm["maximum"]     = 10;
        props["limit"]    = lm;

        Json::Value req(Json::arrayValue);
        req.append("name_keyword");

        Json::Value params;
        params["type"]       = "object";
        params["properties"] = props;
        params["required"]   = req;
        fn["parameters"] = params;

        Json::Value tool;
        tool["type"]     = "function";
        tool["function"] = fn;
        tools.append(tool);
    }

    // ── 工具 3：get_online_status ────────────────────────────────────────
    {
        Json::Value fn;
        fn["name"] = "get_online_status";
        fn["description"] =
            "查询指定 uid 的用户当前是否在线。"
            "【仅在用户明确询问某人是否在线时调用；必须先知道对方的 uid，"
            "若只知道名字请先调用 query_friend_info 获取 uid。】";

        Json::Value props;
        Json::Value uid_prop;
        uid_prop["type"]        = "integer";
        uid_prop["description"] = "要查询在线状态的用户 uid（正整数）";
        uid_prop["minimum"]     = 1;
        props["target_uid"]     = uid_prop;

        Json::Value req(Json::arrayValue);
        req.append("target_uid");

        Json::Value params;
        params["type"]       = "object";
        params["properties"] = props;
        params["required"]   = req;
        fn["parameters"] = params;

        Json::Value tool;
        tool["type"]     = "function";
        tool["function"] = fn;
        tools.append(tool);
    }

    // ── 工具 4：search_chat_history ──────────────────────────────────────
    {
        Json::Value fn;
        fn["name"] = "search_chat_history";
        fn["description"] =
            "搜索当前用户与某位好友之间的聊天记录，支持关键词过滤，按时间正序返回最近匹配消息。"
            "【仅在用户明确要求查找聊天记录时调用。"
            "uid2 填 0 表示搜索与 AI 助手的对话；"
            "若只知道对方名字不知道 uid，请先调用 query_friend_info 获取 uid。"
            "注意：uid1（发起方）由系统自动填入，禁止在参数中传递 uid1。】";

        Json::Value props;

        // uid1 不再暴露给 LLM，由服务端从 caller_uid 固定注入，防止伪造发起方
        Json::Value u2;
        u2["type"]        = "integer";
        u2["description"] = "对话另一方的 uid（必须是好友）；填 0 表示搜索与 AI 助手的对话";
        u2["minimum"]     = 0;
        props["uid2"] = u2;

        Json::Value fnm;
        fnm["type"]        = "string";
        fnm["description"] = "friend name if uid2 is unknown";
        props["friend_name"] = fnm;

        Json::Value kw;
        kw["type"]        = "string";
        kw["description"] = "搜索关键词；空字符串表示不过滤，返回最近的消息";
        props["keyword"] = kw;

        Json::Value lm;
        lm["type"]        = "integer";
        lm["description"] = "最多返回的消息条数，默认 5，最大 20";
        lm["default"]     = 5;
        lm["minimum"]     = 1;
        lm["maximum"]     = 20;
        props["limit"] = lm;

        Json::Value req(Json::arrayValue);

        Json::Value params;
        params["type"]       = "object";
        params["properties"] = props;
        params["required"]   = req;
        fn["parameters"] = params;

        Json::Value tool;
        tool["type"]     = "function";
        tool["function"] = fn;
        tools.append(tool);
    }

    return tools;
}

// ============================================================
// _launchRequest  ──  unified session factory  (Agent Loop)
//
//   allow_tool_calls=true  →  Pass-1: AIHttpSession   stream=false
//      If LLM returns tool_calls → execute tools → loop back to Pass-1
//         (up to AgentTask::max_steps rounds)
//      If LLM returns direct text → finalize (non-streaming)
//   allow_tool_calls=false →  Pass-2: AIStreamSession  stream=true
//      Used as forced final answer when step limit is reached.
// ============================================================
void AgentManager::_launchRequest(int from_uid,
                                  const Json::Value& messages,
                                  bool allow_tool_calls)
{
    _runtime.MarkActing(from_uid,
                        allow_tool_calls ? "pass1_tool_detection" : "pass2_streaming");
    auto self = AgentManager::GetInstance();
    auto& ioc = AsioioservicePool::GetInstance()->Get_iocontext();
    auto finalize_assistant_reply =
        [self, from_uid](const std::string& raw_answer) {
            if (raw_answer.empty()) {
                return;
            }

            AgentTask task;
            const bool has_task = self->_runtime.TryGetTask(from_uid, task);
            const bool draft_mode = has_task && task.pending_action == kActionDraftSendText;
            const std::string draft_body =
                draft_mode ? extract_draft_body(raw_answer) : std::string();
            const bool waiting_confirm =
                draft_mode && task.current_peer_uid > 0 && !draft_body.empty();
            const bool clarification_reply =
                draft_mode && task.current_peer_uid > 0 &&
                draft_body.empty() && is_likely_draft_clarification(raw_answer);
            const bool draft_format_invalid =
                draft_mode && task.current_peer_uid > 0 &&
                draft_body.empty() && !clarification_reply;
            const std::string final_answer =
                waiting_confirm ? draft_body : raw_answer;

            if (draft_format_invalid) {
                const std::string note =
                    "Draft generation did not return the expected format. "
                    "Please retry the request, or specify the tone and key point more clearly.";
                {
                    std::lock_guard<std::mutex> lk(self->_pool_mutex);
                    self->_appendMessage(from_uid, "assistant", note);
                }
                self->_runtime.MarkFailure(from_uid, "draft_format_invalid");
                DbWriteQueue::GetInstance()->PostWrite([from_uid, note]() {
                    AgentManager::_saveBotReplyToDb(from_uid, note);
                });
                emit_bot_message(from_uid, note);
                return;
            }

            {
                std::lock_guard<std::mutex> lk(self->_pool_mutex);
                self->_appendMessage(from_uid, "assistant", final_answer);
            }
            DbWriteQueue::GetInstance()->PostWrite([from_uid, final_answer]() {
                AgentManager::_saveBotReplyToDb(from_uid, final_answer);
            });

            if (waiting_confirm) {
                const std::string payload =
                    build_send_payload_json(task.current_peer_uid, final_answer);
                const std::string confirm_note =
                    build_send_confirm_note(task.current_peer_uid, final_answer);
                self->_runtime.MarkWaitingUserConfirm(
                    from_uid, kActionSendTextMessage, payload);
                {
                    std::lock_guard<std::mutex> lk(self->_pool_mutex);
                    self->_appendMessage(from_uid, "assistant", confirm_note);
                }
                DbWriteQueue::GetInstance()->PostWrite([from_uid, confirm_note]() {
                    AgentManager::_saveBotReplyToDb(from_uid, confirm_note);
                });
                emit_bot_message(from_uid, confirm_note);
                return;
            }

            self->_runtime.MarkAssistantReply(from_uid, final_answer);
        };

    // ── Pass-1: non-streaming, tool call detection ────────────────────────
    if (allow_tool_calls) {
        Json::Value bj;
        bj["model"]       = _model_name;
        bj["stream"]      = false;
        bj["messages"]    = messages;
        bj["tools"]       = _buildToolsArray();
        bj["tool_choice"] = "auto";
        Json::FastWriter fw; std::string bs = fw.write(bj);
        if (!bs.empty()&&bs.back()=='\n') bs.pop_back();

        AIHttpSession::AnswerCallback on_ans =
            [finalize_assistant_reply](const std::string& answer) {
                finalize_assistant_reply(answer);
            };

        AIHttpSession::ToolCallCallback on_tc =
            [self, from_uid](const Json::Value& asst_msg) {
                const Json::Value& tc_arr = asst_msg["tool_calls"];
                if (!tc_arr.isArray()||tc_arr.empty()) return;

                std::cout << "[Agent][Tool] tool_calls count=" << tc_arr.size()
                          << " uid=" << from_uid << "\n";

                // 预先提取工具调用元数据（在当前回调线程读取 Json，安全）
                Json::FastWriter fw2;
                std::string tc_json = fw2.write(tc_arr);
                if (!tc_json.empty()&&tc_json.back()=='\n') tc_json.pop_back();
                const std::string ac =
                    asst_msg.isMember("content")&&asst_msg["content"].isString()
                        ? asst_msg["content"].asString() : "";

                struct TCall { std::string id, name, args_str; };
                std::vector<TCall> calls;
                for (const auto& tc : tc_arr) {
                    calls.push_back({tc["id"].asString(),
                                     tc["function"]["name"].asString(),
                                     tc["function"]["arguments"].asString()});
                    std::cout << "[Agent][Tool] name=" << calls.back().name
                              << " args=" << calls.back().args_str << "\n";
                }
                AgentTask task;
                if (!self->_runtime.TryGetTask(from_uid, task) ||
                    task.pending_action != kActionDraftSendText) {
                    self->_runtime.MarkPlanning(from_uid, "tool_dispatch");
                }
                self->_runtime.MarkToolScheduled(
                    from_uid,
                    calls.size() == 1 ? calls.front().name : "multi_tool_call");

                // 工具执行投递给 DbReadQueue 工作线程（可含同步 MySQL/Redis），
                // 执行完后续体回逻辑线程写记忆池并发起 Pass-2，不阻塞网络/逻辑线程。
                DbReadQueue::GetInstance()->PostRead(
                    [self, from_uid, tc_json = std::move(tc_json), ac,
                     calls = std::move(calls)]() {
                        // ── DbReadQueue 工作线程：执行所有工具调用 ──
                        struct TR { std::string id, result; };
                        std::vector<TR> results;
                        for (const auto& c : calls) {
                            Json::Value args = Json::objectValue;
                            if (!c.args_str.empty()) { Json::Reader r; r.parse(c.args_str, args); }
                            std::string result = AgentManager::_dispatchTool(c.name, args, from_uid);
                            std::cout << "[Agent][Tool] result for " << c.name
                                      << " => " << result << "\n";
                            self->_runtime.MarkToolResult(from_uid, c.name, result);
                            results.push_back({c.id, std::move(result)});
                        }

                        // ── 逻辑线程续体：写入记忆池并发起 Pass-2 流式请求 ──
                        ChatLogicSystem::GetInstance()->PostTask(
                            std::weak_ptr<CSession>{},
                            [self, from_uid, tc_json, ac, results = std::move(results)]() {
                                Json::Value p2_msgs;
                                {
                                    std::lock_guard<std::mutex> lk(self->_pool_mutex);
                                    self->_appendToolCallMsg(from_uid, tc_json, ac);
                                    for (const auto& r : results)
                                        self->_appendToolResultMsg(from_uid, r.id, r.result);
                                    p2_msgs = self->_buildMessagesArray(from_uid);
                                }
                                std::cout << "[Agent][Tool] launching pass-2 stream uid="
                                          << from_uid << "\n";
                                // ── Agent Loop: check step limit ──
                                // If under step limit, go back to Pass-1 (tools enabled)
                                // so the LLM can call more tools if needed.
                                // If at/over limit, force Pass-2 streaming (no tools)
                                // to guarantee a final answer.
                                AgentTask loop_task;
                                bool allow_more_tools = false;
                                if (self->_runtime.TryGetTask(from_uid, loop_task)) {
                                    allow_more_tools =
                                        (loop_task.step_index < loop_task.max_steps);
                                }
                                std::cout << "[Agent][Loop] uid=" << from_uid
                                          << " step=" << loop_task.step_index
                                          << "/" << loop_task.max_steps
                                          << " allow_more_tools="
                                          << allow_more_tools << "\n";
                                self->_launchRequest(
                                    from_uid, p2_msgs, allow_more_tools);
                            });
                    });
            };

        std::make_shared<AIHttpSession>(
            ioc, *_ssl_ctx, from_uid,
            _host, _port, _target, bs, _api_key,
            std::move(on_ans), std::move(on_tc)
        )->Start();

    // ── Pass-2: streaming, final answer ──────────────────────────────────
    } else {
        Json::Value bj;
        bj["model"]    = _model_name;
        bj["stream"]   = true;   // enable SSE streaming
        bj["messages"] = messages;
        // No "tools" field → LLM cannot recurse into tool calls
        Json::FastWriter fw; std::string bs = fw.write(bj);
        if (!bs.empty()&&bs.back()=='\n') bs.pop_back();

        // Generate a stable msg_id for this stream
        static const int64_t s_bt = static_cast<int64_t>(::time(nullptr));
        static std::atomic<uint64_t> s_sq{1};
        const std::string uid_str = std::to_string(from_uid);
        const std::string sid = "stream_9999_" + uid_str + "_"
                              + std::to_string(s_bt) + "_"
                              + std::to_string(s_sq.fetch_add(1));

        AIStreamSession::DoneCallback on_done =
            [finalize_assistant_reply](const std::string& full) {
                finalize_assistant_reply(full);
            };

        std::make_shared<AIStreamSession>(
            ioc, *_ssl_ctx, from_uid,
            _host, _port, _target, bs, _api_key,
            sid, std::move(on_done)
        )->Start();
    }
}

void AgentManager::_scheduleForcedHistoryLookup(int from_uid, const std::string& text)
{
    constexpr int kForcedLimit = 20;
    int peer_hint = detect_forced_history_uid2(text);
    std::string friend_name;
    if (peer_hint < 0) {
        int last_peer = 0;
        {
            std::lock_guard<std::mutex> lk(_pool_mutex);
            const auto it = _last_history_peer.find(from_uid);
            if (it != _last_history_peer.end()) {
                last_peer = it->second;
            }
        }
        if (last_peer > 0 && should_reuse_last_history_peer(text)) {
            peer_hint = last_peer;
        } else {
            friend_name = text;
        }
    }
    const std::string tc_json =
        build_forced_history_tool_call_json(friend_name, peer_hint, kForcedLimit);

    std::cout << "[Agent][Tool] force search_chat_history uid=" << from_uid
              << " peer_hint=" << peer_hint << " text=\"" << text << "\"\n";
    AgentTask task;
    if (!_runtime.TryGetTask(from_uid, task) ||
        task.pending_action != kActionDraftSendText) {
        _runtime.MarkPlanning(from_uid, "force_search_chat_history");
    }
    _runtime.MarkToolScheduled(from_uid, "search_chat_history");

    DbReadQueue::GetInstance()->PostRead(
        [self = GetInstance(), from_uid, peer_hint, friend_name, tc_json, kForcedLimit]() {
            const std::string result = AgentManager::search_chat_history(
                from_uid, peer_hint, friend_name, std::string(), kForcedLimit);
            self->_runtime.MarkToolResult(from_uid, "search_chat_history", result);

            ChatLogicSystem::GetInstance()->PostTask(
                std::weak_ptr<CSession>{},
                [self, from_uid, tc_json, result]() {
                    Json::Value messages;
                    {
                        std::lock_guard<std::mutex> lk(self->_pool_mutex);
                        self->_appendToolCallMsg(from_uid, tc_json);
                        self->_appendToolResultMsg(from_uid,
                                                   "forced_search_chat_history",
                                                   result);
                        messages = self->_buildMessagesArray(from_uid);
                    }
                    std::cout << "[Agent][Tool] launching forced pass-2 stream uid="
                              << from_uid << "\n";
                    // Agent Loop: allow more tool calls if under step limit
                    AgentTask loop_task;
                    bool allow_more_tools = false;
                    if (self->_runtime.TryGetTask(from_uid, loop_task)) {
                        allow_more_tools =
                            (loop_task.step_index < loop_task.max_steps);
                    }
                    std::cout << "[Agent][Loop] forced-history uid=" << from_uid
                              << " step=" << loop_task.step_index
                              << "/" << loop_task.max_steps
                              << " allow_more_tools="
                              << allow_more_tools << "\n";
                    self->_launchRequest(from_uid, messages, allow_more_tools);
                });
        });
}

// ============================================================
// onReceiveMessageFromUser  ──  main entry point
// 若记忆池已有该用户历史直接追加消息并发起请求，不阻塞逻辑线程。
// 若为首次对话（服务重启后），将历史读取投递给 DbReadQueue 工作线程，
// 续体回逻辑线程后再发起请求，全程无同步 DB 阻塞。
// ============================================================
void AgentManager::onReceiveMessageFromUser(int from_uid, const std::string& text)
{
    AgentTask existing_task;
    bool resume_draft_send = false;
    bool user_message_already_appended = false;
    if (_runtime.TryGetTask(from_uid, existing_task) &&
        existing_task.state == AgentTaskState::WaitingUserConfirm &&
        existing_task.pending_action == kActionSendTextMessage) {
        {
            std::lock_guard<std::mutex> lk(_pool_mutex);
            _appendMessage(from_uid, "user", text);
            user_message_already_appended = true;
        }

        if (is_confirm_send_text(text)) {
            int peer_uid = 0;
            std::string draft_text;
            if (!parse_send_payload_json(existing_task.pending_payload_json, peer_uid, draft_text)) {
                const std::string note =
                    "Pending draft expired. Please ask the assistant to draft it again.";
                {
                    std::lock_guard<std::mutex> lk(_pool_mutex);
                    _appendMessage(from_uid, "assistant", note);
                }
                _runtime.MarkFailure(from_uid, "invalid_send_payload");
                DbWriteQueue::GetInstance()->PostWrite([from_uid, note]() {
                    AgentManager::_saveBotReplyToDb(from_uid, note);
                });
                emit_bot_message(from_uid, note);
                return;
            }

            SendTextResult send_result;
            if (!send_text_as_user(from_uid, peer_uid, draft_text, send_result)) {
                const std::string note = "Send failed: " + send_result.error;
                {
                    std::lock_guard<std::mutex> lk(_pool_mutex);
                    _appendMessage(from_uid, "assistant", note);
                }
                _runtime.MarkFailure(from_uid, send_result.error);
                DbWriteQueue::GetInstance()->PostWrite([from_uid, note]() {
                    AgentManager::_saveBotReplyToDb(from_uid, note);
                });
                emit_bot_message(from_uid, note);
                return;
            }

            const std::string note = build_send_result_note(peer_uid, send_result);
            {
                std::lock_guard<std::mutex> lk(_pool_mutex);
                _appendMessage(from_uid, "assistant", note);
            }
            _runtime.MarkAssistantReply(from_uid, note);
            DbWriteQueue::GetInstance()->PostWrite([from_uid, note]() {
                AgentManager::_saveBotReplyToDb(from_uid, note);
            });
            emit_bot_message(from_uid, note);
            return;
        }

        if (is_cancel_send_text(text)) {
            const std::string note = "Send cancelled.";
            {
                std::lock_guard<std::mutex> lk(_pool_mutex);
                _appendMessage(from_uid, "assistant", note);
            }
            _runtime.MarkCancelled(from_uid, "send_cancelled");
            DbWriteQueue::GetInstance()->PostWrite([from_uid, note]() {
                AgentManager::_saveBotReplyToDb(from_uid, note);
            });
            emit_bot_message(from_uid, note);
            return;
        }

        resume_draft_send = true;
    }

    const bool wants_draft_send = resume_draft_send || should_draft_reply_message(text);
    _runtime.OnUserMessage(from_uid, text);
    if (wants_draft_send) {
        _runtime.MarkPlanning(from_uid, kActionDraftSendText);
    }

    int direct_peer_uid = 0;
    std::string direct_draft;
    if (wants_draft_send &&
        try_build_direct_send_draft(from_uid, text, direct_peer_uid, direct_draft)) {
        const std::string payload = build_send_payload_json(direct_peer_uid, direct_draft);
        const std::string confirm_note = build_send_confirm_note(direct_peer_uid, direct_draft);
        {
            std::lock_guard<std::mutex> lk(_pool_mutex);
            if (_memory_pool.count(from_uid) == 0) {
                _memory_pool[from_uid];
            }
            if (!user_message_already_appended) {
                _appendMessage(from_uid, "user", text);
            }
            _appendMessage(from_uid, "assistant", confirm_note);
        }
        _runtime.MarkWaitingUserConfirm(from_uid, kActionSendTextMessage, payload);
        DbWriteQueue::GetInstance()->PostWrite([from_uid, confirm_note]() {
            AgentManager::_saveBotReplyToDb(from_uid, confirm_note);
        });
        emit_bot_message(from_uid, confirm_note);
        return;
    }

    const bool force_history_lookup =
        should_force_search_chat_history(text) || (wants_draft_send && !resume_draft_send);
    bool needs_load = false;
    bool is_still_loading = false;
    Json::Value messages;
    {
        std::lock_guard<std::mutex> lk(_pool_mutex);
        needs_load = (_memory_pool.count(from_uid) == 0);
        if (!needs_load) {
            // If a DB load is still in-flight for this user, the memory pool
            // is just a sentinel (empty).  Don't fire a request with empty
            // context — tell the user to wait.
            if (_loading_uids.count(from_uid)) {
                is_still_loading = true;
            } else {
                if (!user_message_already_appended) {
                    _appendMessage(from_uid, "user", text);
                }
                messages = _buildMessagesArray(from_uid);
            }
        } else {
            // 创建 sentinel，防止并发触发对同一用户的重复加载
            _memory_pool[from_uid];
            _loading_uids.insert(from_uid);
        }
    }

    if (is_still_loading) {
        const std::string note =
            "Your conversation history is still loading. "
            "Please resend your message in a moment.";
        {
            std::lock_guard<std::mutex> lk(_pool_mutex);
            _appendMessage(from_uid, "assistant", note);
        }
        DbWriteQueue::GetInstance()->PostWrite([from_uid, note]() {
            AgentManager::_saveBotReplyToDb(from_uid, note);
        });
        emit_bot_message(from_uid, note);
        return;
    }

    if (!needs_load) {
        if (force_history_lookup) {
            _scheduleForcedHistoryLookup(from_uid, text);
            return;
        }
        _launchRequest(from_uid, messages, /*allow_tool_calls=*/true);
        return;
    }

    // 首次对话：异步从 DB 加载历史记忆，不阻塞当前逻辑线程
    DbReadQueue::GetInstance()->PostRead(
        [self = GetInstance(), from_uid, text, force_history_lookup]() {
            // ── DbReadQueue 工作线程：执行 SELECT ──
            std::vector<std::pair<int, std::string>> rows;
            try {
                auto db = MysqlConPool::GetInstance()->GetSession();
                if (db) {
                    MysqlObj o(std::move(db));
                    auto res = o.get()
                        ->getSchema(MysqlConPool::GetInstance()->GetSchema())
                        .getTable("chat_messages")
                        .select("from_uid", "content")
                        .where("(from_uid = :uid AND to_uid = :bot) OR "
                               "(from_uid = :bot AND to_uid = :uid)")
                        .orderBy("created_at DESC")
                        .limit(AgentManager::MAX_HISTORY_SIZE)
                        .bind("uid", from_uid)
                        .bind("bot", AgentManager::BOT_UID)
                        .execute();
                    for (auto row = res.fetchOne(); !row.isNull(); row = res.fetchOne())
                        rows.push_back({static_cast<int>(row[0]), row[1].get<std::string>()});
                    std::reverse(rows.begin(), rows.end());
                }
            } catch (const std::exception& e) {
                std::cerr << "[Agent] History load error: " << e.what() << "\n";
            }

            // ── 逻辑线程续体：填充记忆池，追加消息，发起请求 ──
            ChatLogicSystem::GetInstance()->PostTask(
                std::weak_ptr<CSession>{},
                [self, from_uid, text, force_history_lookup, rows = std::move(rows)]() {
                    Json::Value messages;
                    {
                        std::lock_guard<std::mutex> lk(self->_pool_mutex);
                        auto& hist = self->_memory_pool[from_uid]; // sentinel 已存在
                        for (auto& [fuid, content] : rows) {
                            AgentMessage m;
                            m.role    = (fuid == from_uid) ? "user" : "assistant";
                            m.content = std::move(content);
                            hist.push_back(std::move(m));
                        }
                        std::cout << "[Agent] Loaded " << hist.size()
                                  << " history for uid=" << from_uid << "\n";
                        self->_appendMessage(from_uid, "user", text);
                        messages = self->_buildMessagesArray(from_uid);
                        // DB load complete — allow subsequent messages to proceed normally
                        self->_loading_uids.erase(from_uid);
                    }
                    if (force_history_lookup) {
                        self->_scheduleForcedHistoryLookup(from_uid, text);
                        return;
                    }
                    self->_launchRequest(from_uid, messages, true);
                });
        });
}
