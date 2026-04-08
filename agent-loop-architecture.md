# AI AgentLoop 设计文档

## 1. 文档目标

本文档面向当前项目的 AI 小助手能力，目标是在现有 `ChatServer` 和 `client` 结构上，设计一套可落地的 `AgentLoop` 方案，让 AI 从“能聊天、能查历史”升级成“能理解任务、分步执行、可确认执行动作、具备上下文延续”的实用 Agent。

本文档重点回答 4 个问题：

- 你当前 AI 模块已经具备哪些基础能力
- 一个好用的 IM AgentLoop 应该长什么样
- 你的项目里最适合优先补哪些工具和状态
- 这些能力应该落在哪些类和文件中

非目标：

- 本文档不直接实现全部代码
- 本文档不讨论大模型厂商切换
- 本文档不优先覆盖网页端 Agent UI

---

## 2. 当前 AI 模块现状

你现在的 AI 模块核心已经集中在 `ChatServer/AgentManager.cpp` 和 `ChatServer/AgentManager.h`。

当前已具备的能力：

- `BOT_UID = 9999` 的 AI 助手身份
- `onReceiveMessageFromUser()` 作为统一入口
- 记忆池 `_memory_pool`
- 工具调用分发 `_dispatchTool()`
- 两段式请求
  - Pass-1: 非流式，允许模型返回 tool calls
  - Pass-2: 流式，基于工具结果生成最终答案
- 已有工具
  - `get_server_time`
  - `query_friend_info`
  - `get_online_status`
  - `search_chat_history`
- 已做的一些上下文修复
  - 强制历史查询
  - 最近历史目标缓存 `_last_history_peer`

这说明你已经不是“普通聊天机器人”了，而是一个 Agent 雏形。但它还缺少完整的 loop、任务状态、确认机制和更强的执行工具。

---

## 3. 什么叫 AgentLoop

对你这个项目来说，`AgentLoop` 的本质不是“让模型多说一点”，而是：

1. 识别当前用户目标
2. 判断是否需要查工具或执行动作
3. 分步骤执行
4. 观察工具结果
5. 如果没完成，继续下一步
6. 如果有副作用，先等待用户确认
7. 最终输出结果并收尾

一个最小可用的 loop 可以抽象成：

```text
Observe -> Plan -> Act -> Observe -> Re-plan -> Stop
```

这层能力比单次 tool call 更重要，因为真实用户经常会说：

- “帮我总结我和某人的聊天，再提炼 3 条待办”
- “帮我草拟一段回复，然后发给他”
- “继续展开第二点”
- “把刚才的总结发到群里”

这些都不是一次函数调用能解决的，而是多步任务。

---

## 4. 适合当前项目的总体架构

建议不要把所有逻辑继续堆在 `AgentManager` 里，而是在 `ChatServer` 内新增一层 Agent 运行时。

推荐结构：

```text
ChatServer/
  Agent/
    AgentRuntime.h
    AgentRuntime.cpp
    AgentTaskStore.h
    AgentTaskStore.cpp
    AgentToolRegistry.h
    AgentToolRegistry.cpp
    AgentPolicy.h
    AgentPolicy.cpp
  AgentManager.h
  AgentManager.cpp
```

职责建议：

- `AgentManager`
  - 继续作为统一入口
  - 负责接消息、发送流式结果、维护基础 memory
  - 不再独自承担全部 agent 编排

- `AgentRuntime`
  - 负责单个用户当前任务的状态机
  - 负责 loop 的每一步推进
  - 控制最大步数、超时、失败收口

- `AgentToolRegistry`
  - 统一注册工具元数据
  - 区分只读工具、草拟工具、危险工具
  - 统一做参数校验和执行分发

- `AgentTaskStore`
  - 存储任务态、上一步结果、上下文锚点
  - 支持“继续”“接着做”“发给他”这种续问

- `AgentPolicy`
  - 规定何时要用户确认
  - 规定哪些工具可自动执行
  - 规定 loop 停止条件

---

## 5. 推荐状态机

### 5.1 最小状态

建议一个任务至少有以下状态：

- `idle`
- `planning`
- `acting`
- `waiting_tool`
- `waiting_user_confirm`
- `waiting_user_input`
- `done`
- `failed`
- `cancelled`

### 5.2 典型流转

```text
idle
  -> planning
  -> acting
  -> waiting_tool
  -> planning
  -> waiting_user_confirm
  -> acting
  -> done
```

或者：

```text
idle
  -> planning
  -> acting
  -> failed
```

### 5.3 为什么你需要这层

没有状态机时，AI 很容易出现：

- 一句话里想做两件事，但只做了第一件
- 工具失败后直接胡编结果
- 有副作用操作直接执行，缺少确认
- 用户说“继续”，系统不知道是在续哪个任务

状态机本质上是在给 Agent 一个“任务生命周期”。

---

## 6. 推荐任务模型

建议定义一个最小 `AgentTask`：

```cpp
struct AgentTask {
    std::string task_id;
    int owner_uid = 0;
    std::string user_goal;
    std::string state;
    int step_index = 0;
    int max_steps = 5;
    int current_peer_uid = 0;
    int current_group_id = 0;
    std::string pending_action;
    std::string pending_payload_json;
    std::string last_tool_name;
    std::string last_tool_result_json;
    int64_t created_at_ms = 0;
    int64_t updated_at_ms = 0;
};
```

这里最重要的不是字段多，而是这几个锚点：

- `current_peer_uid`
- `current_group_id`
- `pending_action`
- `last_tool_result_json`

这几个字段会直接决定“继续总结”“发给他”“拉这个群”“基于刚才的结果再做下一步”是否能成立。

---

## 7. 推荐的 AgentLoop 运行流程

建议 loop 采用“最多 3 到 5 步”的保守模式。

### 7.1 流程

1. 接收用户消息
2. 判断是否命中已有任务上下文
3. 生成当前 step 目标
4. 选择一个工具或直接回答
5. 执行工具
6. 记录观察结果
7. 判断是否结束
8. 未结束则进入下一步
9. 超步数则停止并返回阶段结果

### 7.2 伪代码

```cpp
for (int step = 0; step < max_steps; ++step) {
    auto plan = planner.MakePlan(task, memory);
    if (plan.need_user_confirm) {
        task.state = "waiting_user_confirm";
        return AskUserConfirm(plan);
    }
    if (plan.need_user_input) {
        task.state = "waiting_user_input";
        return AskUserForMoreInfo(plan);
    }
    if (plan.direct_answer) {
        task.state = "done";
        return Reply(plan.answer);
    }

    auto result = tool_registry.Execute(plan.tool_name, plan.arguments);
    task.last_tool_name = plan.tool_name;
    task.last_tool_result_json = result;

    if (planner.IsTaskDone(task, result)) {
        task.state = "done";
        return Reply(planner.FinalAnswer(task, result));
    }
}

task.state = "failed";
return Reply("这项任务我先完成到这里，建议你进一步确认。");
```

---

## 8. 工具设计原则

### 8.1 三类工具

建议把工具分成 3 类：

- 只读工具
  - 可以自动执行
  - 例如查聊天记录、查好友资料、查未读数

- 草拟工具
  - 可以自动执行
  - 例如生成回复草稿、提炼待办、总结聊天

- 有副作用工具
  - 必须确认后执行
  - 例如发消息、建群、邀请成员、退群、删除会话

### 8.2 工具不要直接暴露底层 DAO

工具层应暴露业务能力，而不是暴露数据库查询细节。

推荐暴露这种名字：

- `get_recent_conversations`
- `search_friend_chat_history`
- `search_group_chat_history`
- `summarize_conversation`
- `extract_todo_from_chat`
- `draft_reply_message`
- `send_message_with_confirm`
- `create_group_with_confirm`
- `invite_group_member_with_confirm`

不要直接做成：

- `query_chat_messages_table`
- `select_friends`

前者是 Agent 能理解的动作，后者只是底层实现。

---

## 9. 最适合你当前项目先加的工具

### 9.1 只读工具

- `get_recent_conversations`
  - 获取最近会话摘要
  - 解决“最近谁找过我”

- `search_group_chat_history`
  - 群聊摘要必须有这个

- `get_unread_summary`
  - 适合登录后统一摘要未读

- `get_shared_files_with_friend`
  - 查询某个好友最近发过哪些文件

- `get_call_history`
  - 后续可扩展到音视频通话记录

### 9.2 草拟工具

- `summarize_conversation`
  - 对聊天记录做摘要

- `extract_todo_from_chat`
  - 从聊天中提取待办、截止时间、负责人

- `draft_reply_message`
  - “帮我礼貌地回复一下”

- `rewrite_message_tone`
  - “把这句话变得更正式/更简短”

### 9.3 必须确认的执行工具

- `send_message_with_confirm`
- `create_group_with_confirm`
- `invite_group_member_with_confirm`
- `leave_group_with_confirm`
- `pin_conversation_with_confirm`

这些工具是让 Agent 真正“有用”的关键，但必须有确认机制。

---

## 10. 上下文和记忆设计

你现在已经有 `_memory_pool`，但它还偏向“对话记忆”，不够支撑完整 Agent。

建议记忆拆成 3 层。

### 10.1 对话记忆

继续使用当前 `_memory_pool`，用于给模型看最近上下文。

适合保存：

- 用户原话
- assistant 回答
- tool call
- tool result

### 10.2 任务记忆

用于支撑续问和多步执行。

适合保存：

- 当前任务目标
- 当前 peer/group
- 上次工具结果
- pending action
- 当前是否在等用户确认

这层建议放到 `AgentTaskStore`。

### 10.3 长期偏好记忆

这层不是第一优先级，但后面很值当。

适合保存：

- 用户偏好的回复风格
- 常联系对象
- 常用群
- 是否喜欢简短总结
- 是否默认先草拟再确认

这层可以后面落数据库，不建议一开始就做复杂。

---

## 11. 确认机制设计

这是你做“能执行动作的 Agent”时最重要的一层。

### 11.1 哪些动作必须确认

以下操作建议一律确认：

- 真正发送消息
- 建群
- 邀请成员
- 退群
- 删除会话
- 修改群资料

### 11.2 确认消息格式

可以给客户端发一个标准结构：

```json
{
  "type": "agent_confirm",
  "task_id": "task_xxx",
  "action": "send_message",
  "target_uid": 1004,
  "preview": "好的，我明天下午三点前给你答复。",
  "hint": "确认后将发送给 EDPerson1"
}
```

用户确认后再回一个：

```json
{
  "type": "agent_confirm_reply",
  "task_id": "task_xxx",
  "approved": true
}
```

### 11.3 为什么必须这样做

否则 Agent 很快会变得“不可信”：

- 用户只是想草拟，结果被直接发出
- 用户只是探索方案，系统却做了真实动作
- 出错时难以追责

---

## 12. 客户端建议补的能力

真正好用的 Agent 不是只有服务端，客户端也要能承接状态。

建议在 `client` 侧补 3 类展示。

### 12.1 Agent 状态提示

例如：

- 正在理解任务
- 正在查聊天记录
- 正在整理总结
- 等待你确认发送

### 12.2 工具结果卡片

比如：

- 聊天摘要卡片
- 待办清单卡片
- 草拟消息卡片
- 确认发送卡片

### 12.3 续问入口

例如：

- 继续展开
- 再简短一点
- 发给他
- 转发到群

这样用户不需要每次重新完整描述任务。

---

## 13. 针对你这个 IM 项目最实用的 Agent 场景

### 13.1 聊天总结 Agent

用户说：

- “帮我总结我和张三最近聊了什么”
- “继续展开第二点”
- “把重点整理成 3 条待办”

适合的 loop：

1. 解析目标好友
2. 查聊天记录
3. 做摘要
4. 提炼待办
5. 返回结果

### 13.2 回复助手 Agent

用户说：

- “帮我回他一句更礼貌的”
- “简短一点”
- “确认发送”

适合的 loop：

1. 识别当前会话目标
2. 结合上下文草拟回复
3. 用户调整
4. 用户确认
5. 真正发送

### 13.3 群助手 Agent

用户说：

- “总结这个群最近在讨论什么”
- “帮我创建一个项目组群并拉这几个人”
- “把刚才总结发到群里”

适合的 loop：

1. 确定群或成员名单
2. 拉取群历史或成员信息
3. 草拟动作
4. 二次确认
5. 执行

### 13.4 未读摘要 Agent

用户登录后可以由 Agent 主动提供：

- 最近 5 个未读会话摘要
- 哪些消息可能需要优先回复
- 哪些群里有待办

这会显著提升可用性。

---

## 14. 推荐的数据结构

### 14.1 AgentTask

```cpp
struct AgentTask {
    std::string task_id;
    int owner_uid = 0;
    std::string goal;
    std::string state;
    int step_index = 0;
    int max_steps = 5;
    int current_peer_uid = 0;
    int current_group_id = 0;
    std::string pending_action;
    std::string pending_payload_json;
    std::string last_tool_name;
    std::string last_tool_result_json;
    int64_t created_at_ms = 0;
    int64_t updated_at_ms = 0;
};
```

### 14.2 AgentToolSpec

```cpp
struct AgentToolSpec {
    std::string name;
    std::string description;
    bool read_only = true;
    bool require_confirm = false;
    bool allowed_in_loop = true;
};
```

### 14.3 AgentObservation

```cpp
struct AgentObservation {
    std::string tool_name;
    std::string result_json;
    bool success = true;
    std::string error;
};
```

---

## 15. 对当前代码的落地建议

### 15.1 第一阶段：把 Runtime 独立出来

目标：

- 不再让 `AgentManager` 独自承担所有逻辑
- 把 loop 和状态控制拆出去

建议新增：

- `ChatServer/Agent/AgentRuntime.h`
- `ChatServer/Agent/AgentRuntime.cpp`

先让它支持：

- 单任务单状态机
- 最多 3 步
- 只读工具 loop

### 15.2 第二阶段：补任务存储

建议新增：

- `ChatServer/Agent/AgentTaskStore.h`
- `ChatServer/Agent/AgentTaskStore.cpp`

先做内存版即可，后面再落 MySQL。

第一版存：

- `owner_uid -> current_task`
- `owner_uid -> last_peer_uid`
- `owner_uid -> pending_confirm`

### 15.3 第三阶段：补写操作工具和确认流

建议先实现：

- `draft_reply_message`
- `send_message_with_confirm`

因为这组功能最能体现 Agent 价值，而且实现难度适中。

### 15.4 第四阶段：补客户端 Agent 卡片 UI

在 `client` 里建议加一个轻量模块：

- `client/Manager/AgentTaskMgr.h`
- `client/Manager/AgentTaskMgr.cpp`

用于：

- 接收 agent 状态消息
- 渲染确认卡片
- 发送确认结果

---

## 16. 推荐开发顺序

最适合你的顺序是：

1. 抽出 `AgentRuntime`
2. 加 `AgentTask` 和任务状态机
3. 加 `draft_reply_message`
4. 加 `send_message_with_confirm`
5. 加 `get_recent_conversations`
6. 加 `search_group_chat_history`
7. 加 `extract_todo_from_chat`
8. 最后再做客户端确认卡片和未读摘要

这个顺序的好处是：

- 每一步都能看到实用价值
- 不需要先重构整套聊天系统
- 能逐步验证 AgentLoop 是否真正提升体验

---

## 17. 最小可用版本建议

如果只做一个 MVP，我建议目标定为：

### MVP 功能

- 总结和某好友的聊天
- 提炼待办
- 草拟回复
- 确认后发送
- 支持“继续展开”“再简短一点”“发给他”

### MVP 不急着做

- 自动多轮复杂规划
- 自主联网搜索
- 群管理全量自动化
- 长期个性化记忆

因为对你当前项目来说，先把“好用的 IM 助手”做稳，比追求“大而全的通用 Agent”更值。

---

## 18. 总结

对你这个项目来说，一个好用的 Agent 模块，核心不是更复杂的模型提示词，而是这 5 点：

- 有任务状态机
- 有多步 loop
- 有上下文锚点
- 有业务化工具
- 有副作用确认机制

你当前的 `AgentManager` 已经完成了基础聊天、工具调用和流式回复，下一步最值得投入的方向就是：

从“会调用工具的 AI”升级成“能持续执行任务的 Agent”。

如果继续往下做，最推荐的第一项代码落地是：

- 先抽 `AgentRuntime`
- 再做 `draft_reply_message + send_message_with_confirm`

这会比继续加更多只读工具，更快把体验拉到一个真正好用的水平。
