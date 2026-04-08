#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

enum class AgentTaskState {
    Idle = 0,
    Planning,
    Acting,
    WaitingTool,
    WaitingUserConfirm,
    WaitingUserInput,
    Done,
    Failed,
    Cancelled,
};

struct AgentTask {
    std::string task_id;
    int owner_uid = 0;
    std::string user_goal;
    AgentTaskState state = AgentTaskState::Idle;
    int step_index = 0;
    int max_steps = 5;
    int current_peer_uid = 0;
    std::int64_t current_group_id = 0;
    std::string pending_action;
    std::string pending_payload_json;
    std::string last_tool_name;
    std::string last_tool_result_json;
    std::string last_error;
    std::string last_reply_excerpt;
    std::int64_t created_at_ms = 0;
    std::int64_t updated_at_ms = 0;
};

class AgentRuntime {
public:
    AgentRuntime() = default;

    AgentTask OnUserMessage(int owner_uid, const std::string& text);
    AgentTask MarkPlanning(int owner_uid, const std::string& reason);
    AgentTask MarkActing(int owner_uid, const std::string& reason);
    AgentTask MarkToolScheduled(int owner_uid, const std::string& tool_name);
    AgentTask MarkToolResult(int owner_uid,
                             const std::string& tool_name,
                             const std::string& result_json);
    AgentTask MarkWaitingUserConfirm(int owner_uid,
                                     const std::string& action,
                                     const std::string& payload_json);
    AgentTask MarkWaitingUserInput(int owner_uid, const std::string& reason);
    AgentTask MarkAssistantReply(int owner_uid, const std::string& reply);
    AgentTask MarkFailure(int owner_uid, const std::string& error);
    AgentTask MarkCancelled(int owner_uid, const std::string& reason);

    bool TryGetTask(int owner_uid, AgentTask& out) const;
    void Clear(int owner_uid);

    static const char* ToString(AgentTaskState state);

private:
    AgentTask& EnsureTaskLocked(int owner_uid);
    static std::int64_t NowMs();
    static std::string MakeTaskId(int owner_uid, std::uint64_t seq);
    static std::string BuildExcerpt(const std::string& text, std::size_t max_len = 96);
    static void UpdateAnchorsFromToolResult(AgentTask& task, const std::string& result_json);
    static void LogTask(const char* stage, const AgentTask& task);

    mutable std::mutex mutex_;
    std::unordered_map<int, AgentTask> tasks_;
    std::uint64_t next_task_seq_ = 1;
};
