#include "AgentRuntime.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>

#include <json/json.h>

namespace {

bool IsTerminalState(AgentTaskState state)
{
    return state == AgentTaskState::Done ||
           state == AgentTaskState::Failed ||
           state == AgentTaskState::Cancelled;
}

} // namespace

AgentTask AgentRuntime::OnUserMessage(int owner_uid, const std::string& text)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);

    if (task.task_id.empty() || IsTerminalState(task.state)) {
        task = AgentTask{};
        task.task_id = MakeTaskId(owner_uid, next_task_seq_++);
        task.owner_uid = owner_uid;
        task.created_at_ms = NowMs();
        task.max_steps = 5;
    }

    task.user_goal = text;
    task.state = AgentTaskState::Planning;
    task.pending_action.clear();
    task.pending_payload_json.clear();
    task.last_error.clear();
    task.updated_at_ms = NowMs();
    task.step_index = std::max(1, task.step_index + 1);
    LogTask("user_message", task);
    return task;
}

AgentTask AgentRuntime::MarkPlanning(int owner_uid, const std::string& reason)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::Planning;
    task.pending_action = reason;
    task.updated_at_ms = NowMs();
    LogTask("planning", task);
    return task;
}

AgentTask AgentRuntime::MarkActing(int owner_uid, const std::string& reason)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::Acting;
    task.pending_action = reason;
    task.updated_at_ms = NowMs();
    LogTask("acting", task);
    return task;
}

AgentTask AgentRuntime::MarkToolScheduled(int owner_uid, const std::string& tool_name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::WaitingTool;
    task.last_tool_name = tool_name;
    task.step_index++;
    task.updated_at_ms = NowMs();
    LogTask("tool_scheduled", task);
    return task;
}

AgentTask AgentRuntime::MarkToolResult(int owner_uid,
                                       const std::string& tool_name,
                                       const std::string& result_json)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::Planning;
    task.last_tool_name = tool_name;
    task.last_tool_result_json = result_json;
    task.updated_at_ms = NowMs();
    UpdateAnchorsFromToolResult(task, result_json);
    LogTask("tool_result", task);
    return task;
}

AgentTask AgentRuntime::MarkWaitingUserConfirm(int owner_uid,
                                               const std::string& action,
                                               const std::string& payload_json)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::WaitingUserConfirm;
    task.pending_action = action;
    task.pending_payload_json = payload_json;
    task.updated_at_ms = NowMs();
    LogTask("waiting_confirm", task);
    return task;
}

AgentTask AgentRuntime::MarkWaitingUserInput(int owner_uid, const std::string& reason)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::WaitingUserInput;
    task.pending_action = reason;
    task.updated_at_ms = NowMs();
    LogTask("waiting_input", task);
    return task;
}

AgentTask AgentRuntime::MarkAssistantReply(int owner_uid, const std::string& reply)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::Done;
    task.pending_action.clear();
    task.pending_payload_json.clear();
    task.last_reply_excerpt = BuildExcerpt(reply);
    task.updated_at_ms = NowMs();
    LogTask("assistant_reply", task);
    return task;
}

AgentTask AgentRuntime::MarkFailure(int owner_uid, const std::string& error)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::Failed;
    task.last_error = error;
    task.updated_at_ms = NowMs();
    LogTask("failure", task);
    return task;
}

AgentTask AgentRuntime::MarkCancelled(int owner_uid, const std::string& reason)
{
    std::lock_guard<std::mutex> lk(mutex_);
    AgentTask& task = EnsureTaskLocked(owner_uid);
    task.state = AgentTaskState::Cancelled;
    task.last_error = reason;
    task.updated_at_ms = NowMs();
    LogTask("cancelled", task);
    return task;
}

bool AgentRuntime::TryGetTask(int owner_uid, AgentTask& out) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = tasks_.find(owner_uid);
    if (it == tasks_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

void AgentRuntime::Clear(int owner_uid)
{
    std::lock_guard<std::mutex> lk(mutex_);
    tasks_.erase(owner_uid);
}

const char* AgentRuntime::ToString(AgentTaskState state)
{
    switch (state) {
    case AgentTaskState::Idle:
        return "idle";
    case AgentTaskState::Planning:
        return "planning";
    case AgentTaskState::Acting:
        return "acting";
    case AgentTaskState::WaitingTool:
        return "waiting_tool";
    case AgentTaskState::WaitingUserConfirm:
        return "waiting_user_confirm";
    case AgentTaskState::WaitingUserInput:
        return "waiting_user_input";
    case AgentTaskState::Done:
        return "done";
    case AgentTaskState::Failed:
        return "failed";
    case AgentTaskState::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

AgentTask& AgentRuntime::EnsureTaskLocked(int owner_uid)
{
    AgentTask& task = tasks_[owner_uid];
    if (task.owner_uid == 0) {
        task.owner_uid = owner_uid;
        task.updated_at_ms = NowMs();
    }
    return task;
}

std::int64_t AgentRuntime::NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string AgentRuntime::MakeTaskId(int owner_uid, std::uint64_t seq)
{
    return "task_" + std::to_string(owner_uid) + "_" + std::to_string(NowMs()) + "_" +
           std::to_string(seq);
}

std::string AgentRuntime::BuildExcerpt(const std::string& text, std::size_t max_len)
{
    std::string excerpt = text;
    std::replace(excerpt.begin(), excerpt.end(), '\n', ' ');
    std::replace(excerpt.begin(), excerpt.end(), '\r', ' ');
    if (excerpt.size() > max_len) {
        excerpt.resize(max_len);
        excerpt += "...";
    }
    return excerpt;
}

void AgentRuntime::UpdateAnchorsFromToolResult(AgentTask& task, const std::string& result_json)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    Json::Value root;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader || !reader->parse(result_json.data(),
                                  result_json.data() + result_json.size(),
                                  &root,
                                  &errs)) {
        return;
    }

    if (root.isMember("peer_uid") && root["peer_uid"].isInt()) {
        task.current_peer_uid = root["peer_uid"].asInt();
    }
    if (root.isMember("group_id") && root["group_id"].isInt64()) {
        task.current_group_id = root["group_id"].asInt64();
    }
    if (root.isMember("error") && root["error"].isString()) {
        task.last_error = root["error"].asString();
    }
}

void AgentRuntime::LogTask(const char* stage, const AgentTask& task)
{
    std::cout << "[AgentRuntime] stage=" << stage
              << " uid=" << task.owner_uid
              << " task_id=" << task.task_id
              << " state=" << ToString(task.state)
              << " step=" << task.step_index
              << " peer=" << task.current_peer_uid
              << " group=" << task.current_group_id
              << " tool=" << task.last_tool_name
              << std::endl;
}
