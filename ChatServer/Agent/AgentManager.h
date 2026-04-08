#pragma once

#include "Singleton.h"
#include "AgentRuntime.h"

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <boost/asio/ssl/context.hpp>
#include <json/json.h>

struct AgentMessage {
    std::string role;
    std::string content;
    std::string tool_calls_json;
    std::string tool_call_id;
};

class AgentManager : public Singleton<AgentManager> {
    friend class Singleton<AgentManager>;

public:
    static constexpr int BOT_UID = 9999;
    static constexpr int MAX_HISTORY_SIZE = 20;

    void onReceiveMessageFromUser(int from_uid, const std::string& text);
    void clearMemory(int uid);

    static std::string get_server_time();
    static std::string query_friend_info(int caller_uid,
                                         const std::string& name_keyword,
                                         int limit = 5);
    static std::string get_online_status(int caller_uid, int target_uid);
    static std::string search_chat_history(int caller_uid,
                                           int uid2,
                                           const std::string& friend_name,
                                           const std::string& keyword,
                                           int limit = 5);

private:
    AgentManager();

    std::string _host;
    std::string _port;
    std::string _target;
    std::string _api_key;
    std::string _model_name;

    std::shared_ptr<boost::asio::ssl::context> _ssl_ctx;

    std::mutex _pool_mutex;
    std::unordered_map<int, std::list<AgentMessage>> _memory_pool;
    std::unordered_map<int, int> _last_history_peer;
    std::unordered_set<int> _loading_uids;   // users with DB load in-flight
    AgentRuntime _runtime;

    void _appendMessage(int uid, const std::string& role, const std::string& content);
    void _appendToolCallMsg(int uid,
                            const std::string& serialized_tool_calls,
                            const std::string& content = "");
    void _appendToolResultMsg(int uid,
                              const std::string& tool_call_id,
                              const std::string& result);

    static std::string _dispatchTool(const std::string& name,
                                     const Json::Value& arguments,
                                     int caller_uid);

    Json::Value _buildMessagesArray(int uid) const;
    void _launchRequest(int from_uid,
                        const Json::Value& messages,
                        bool allow_tool_calls);
    void _scheduleForcedHistoryLookup(int from_uid, const std::string& text);

    static void _saveBotReplyToDb(int to_uid, const std::string& text);
    void _loadHistoryFromDb(int uid);
};
