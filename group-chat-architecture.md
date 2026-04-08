# 群聊业务架构设计

## 1. 文档目标

本文档面向当前项目 `client + ChatServer + GateServer + GrpcStatusServer + VarifyServer`，给出一套适合现有代码结构的群聊业务层设计。

目标是：

- 在不推翻现有单聊架构的前提下，新增可扩展的群聊能力。
- 保持 `GateServer` 轻量，群聊核心逻辑集中在 `ChatServer`。
- 复用现有在线状态、跨服路由、消息同步思路。
- 为后续的群公告、管理员、禁言、@、群文件打好结构基础。

非目标是：

- 本阶段不直接重构全部单聊逻辑。
- 本阶段不直接做群语音/群视频。
- 本阶段不优先做管理后台。

---

## 2. 基于当前代码的现状判断

当前项目已经具备做群聊的底座，但数据模型和客户端会话抽象仍然是单聊导向。

### 2.1 服务端现状

- `ChatServer/ChatLogicSystem.cpp` 目前直接处理 `ID_TEXT_CHAT_MSG` 等业务入口，属于“handler 里承载较多业务”的模式。
- `ChatServer/MySqlPool.h` 当前 DAO 主要覆盖用户、好友、文件、单聊历史等能力，尚无正式群聊 DAO。
- `ChatServer/SyncService.h` 已经有“消息正文一份 + Timeline 索引扩散”的思路，这对群聊是正确方向。
- 跨服能力已有基础：用户在线位置可依赖 Redis 和 gRPC 路由。

### 2.2 客户端现状

- `client/Manager/ChatMgr.h` 当前会话历史和未读数都按 `peerUid` 建模，只适合 1:1。
- `client/Utils/ChatStorage.cpp` 的本地 SQLite 表也是按 `peer_uid` 存储，不适合群聊。
- `client/Model/MessageInfo.h` 只有 `fromUid/toUid`，没有“会话类型”和“会话 ID”的抽象。
- `client/Network/tcpmgr.cpp` 当前文本消息接收逻辑默认来自单聊。

### 2.3 结论

要做群聊，最关键的不是先写 UI，而是先完成以下抽象升级：

1. 服务端把群聊业务从 `ChatLogicSystem` 中拆到专门 Service。
2. 客户端把“会话”从 `peerUid` 升级成 `ConversationId`。
3. 数据层使用“正文一份，索引扩散”的群消息模型。

---

## 3. 核心设计原则

### 3.1 群聊是一种会话，不是一堆单聊广播

业务层必须把群聊定义为独立会话类型，而不是“把同一条消息给 N 个用户各发一遍单聊”。

原因：

- 否则服务端和客户端都无法正确维护群资料、成员列表、已读状态、系统通知。
- 群消息撤回、禁言、@、未读数等后续能力会失去统一入口。

### 3.2 Transport 层只做收包和发包，业务放进 Service

`ChatLogicSystem` 应只负责：

- 解析 DTO
- 获取会话身份
- 调用业务 Service
- 回响应或异步推送

不要继续在 handler 中直接写：

- 权限判断
- SQL 事务
- 群成员广播
- 离线逻辑

### 3.3 消息正文只存一份，按成员写索引

群聊消息不应按“每个成员一条正文”存储。正确模型是：

- `group_message_content` 存正文一份
- `group_user_timeline` 按成员写索引

这样做的好处：

- 正文不重复
- 增量同步高效
- 未来可与单聊同步模型统一

### 3.4 在线推送和离线同步分离

发送群消息后：

- 在线成员尽量实时推送
- 离线成员不单独复制一份“离线消息正文”
- 离线成员通过群 Timeline 增量拉取

---

## 4. 推荐的服务端业务层结构

建议在 `ChatServer` 内新增一层正式业务服务，避免群逻辑继续堆在 `ChatLogicSystem.cpp`。

### 4.1 推荐目录

```text
ChatServer/
  DTO/
    GroupDTO.h
  Service/
    GroupService.h
    GroupService.cpp
    GroupMemberService.h
    GroupMemberService.cpp
    GroupMessageService.h
    GroupMessageService.cpp
    ConversationService.h
    ConversationService.cpp
  Repository/
    GroupRepository.h
    GroupRepository.cpp
```

如果你暂时不想新建 `Repository/` 目录，也可以先扩展 `MysqlDao`，但长期看不建议把所有群 DAO 都堆进 `MySqlPool.h`。

### 4.2 各服务职责

#### GroupService

负责：

- 建群
- 解散群
- 修改群名
- 修改头像
- 修改群公告
- 获取群资料

不负责：

- 发消息
- 群成员广播

#### GroupMemberService

负责：

- 邀请成员
- 退群
- 踢人
- 查成员列表
- 角色管理
- 校验是否成员
- 校验是否管理员/群主

角色建议：

- `owner`
- `admin`
- `member`

#### GroupMessageService

负责：

- 校验发送者是否在群内
- 校验是否被禁言
- 写群消息正文
- 给成员写 Timeline
- 在线推送
- 跨服投递
- 群消息同步接口

这是群聊最核心的服务。

#### ConversationService

负责：

- 最近会话
- 最后一条消息摘要
- 未读数
- 会话置顶
- 会话删除或隐藏

这层建议从一开始就抽象成“支持单聊和群聊”的统一会话服务。

---

## 5. 推荐的数据模型

### 5.1 群基础表

```sql
CREATE TABLE chat_group (
    group_id     BIGINT PRIMARY KEY,
    owner_uid    INT NOT NULL,
    group_name   VARCHAR(128) NOT NULL,
    avatar_path  VARCHAR(512) DEFAULT '',
    notice       VARCHAR(1024) DEFAULT '',
    status       TINYINT NOT NULL DEFAULT 1,
    created_at   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
               ON UPDATE CURRENT_TIMESTAMP
);
```

字段说明：

- `status`: 1=normal, 2=dissolved

### 5.2 群成员表

```sql
CREATE TABLE chat_group_member (
    group_id      BIGINT NOT NULL,
    uid           INT NOT NULL,
    role          TINYINT NOT NULL DEFAULT 1,
    status        TINYINT NOT NULL DEFAULT 1,
    mute_until    DATETIME NULL,
    joined_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (group_id, uid),
    INDEX idx_group_member_uid (uid)
);
```

字段建议：

- `role`: 0=owner, 1=admin, 2=member
- `status`: 1=active, 2=quit, 3=kicked

### 5.3 群消息正文表

```sql
CREATE TABLE group_message_content (
    msg_id         BIGINT PRIMARY KEY,
    group_id       BIGINT NOT NULL,
    sender_uid     INT NOT NULL,
    msg_type       TINYINT NOT NULL DEFAULT 0,
    content        TEXT NOT NULL,
    created_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_group_msg (group_id, msg_id)
);
```

字段建议：

- `msg_type`: 0=text, 1=image, 2=file, 3=voice, 9=system

### 5.4 群成员 Timeline 表

```sql
CREATE TABLE group_user_timeline (
    owner_uid      INT NOT NULL,
    group_id       BIGINT NOT NULL,
    msg_id         BIGINT NOT NULL,
    created_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (owner_uid, msg_id),
    INDEX idx_group_conv (owner_uid, group_id, msg_id)
);
```

说明：

- 发送一条群消息时，正文写 `group_message_content` 一次。
- 然后给每个有效成员写一条 `group_user_timeline`。
- 客户端同步某人的群消息时，只拉 `owner_uid = 当前用户` 的 Timeline。

### 5.5 群已读状态表

```sql
CREATE TABLE group_read_state (
    group_id          BIGINT NOT NULL,
    uid               INT NOT NULL,
    last_read_msg_id  BIGINT NOT NULL DEFAULT 0,
    last_read_at      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (group_id, uid)
);
```

这张表不是第一阶段必需，但建议提前预留。

### 5.6 最近会话表

如果你想把单聊和群聊最近会话统一起来，建议新增：

```sql
CREATE TABLE user_conversation (
    owner_uid          INT NOT NULL,
    conv_type          TINYINT NOT NULL,
    conv_id            BIGINT NOT NULL,
    last_msg_id        BIGINT NOT NULL DEFAULT 0,
    unread_count       INT NOT NULL DEFAULT 0,
    is_pinned          TINYINT NOT NULL DEFAULT 0,
    is_muted           TINYINT NOT NULL DEFAULT 0,
    is_hidden          TINYINT NOT NULL DEFAULT 0,
    updated_at         DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
                      ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (owner_uid, conv_type, conv_id)
);
```

字段建议：

- `conv_type`: 1=peer, 2=group

---

## 6. 与现有 SyncService 的关系

当前 `SyncService` 是单聊版的 Timeline 模型，思路是对的，但结构仍然是：

- `peer_uid`
- `direction`

对于群聊，我建议分两步走。

### 6.1 第一阶段

保留现有单聊 `SyncService` 不动，单独新增群聊同步逻辑：

- `GroupMessageService::saveGroupMessage(...)`
- `GroupMessageService::pullGroupMessages(...)`

原因：

- 风险小
- 不会影响当前单聊同步稳定性
- 先把群聊跑通

### 6.2 第二阶段

后续再把单聊和群聊统一成泛化的 `ConversationSyncService`：

- `conv_type`
- `conv_id`
- `msg_id`

这一步适合在群聊稳定后做，不建议第一版就强推。

---

## 7. 推荐的消息号和 DTO 设计

建议在 `2000-2999` 区间预留一段群聊消息号，例如 `2060-2079`。

### 7.1 建议的消息号

```text
ID_CREATE_GROUP_REQ          = 2060
ID_CREATE_GROUP_RSP          = 2061
ID_GROUP_INFO_REQ            = 2062
ID_GROUP_INFO_RSP            = 2063
ID_GROUP_MEMBER_LIST_REQ     = 2064
ID_GROUP_MEMBER_LIST_RSP     = 2065
ID_INVITE_GROUP_MEMBER_REQ   = 2066
ID_INVITE_GROUP_MEMBER_RSP   = 2067
ID_QUIT_GROUP_REQ            = 2068
ID_QUIT_GROUP_RSP            = 2069
ID_GROUP_TEXT_MSG            = 2070
ID_GROUP_TEXT_MSG_RSP        = 2071
ID_GROUP_SYNC_REQ            = 2072
ID_GROUP_SYNC_RSP            = 2073
ID_GROUP_EVENT_NOTIFY        = 2074
```

这些 ID 需要同时写入：

- `ChatServer/const.h`
- `client/Utils/global.h`

### 7.2 DTO 示例

#### 建群请求

```json
{
  "group_name": "项目讨论组",
  "member_uids": [1002, 1003, 1004]
}
```

#### 建群响应

```json
{
  "error": 0,
  "group_id": 90000001,
  "group_name": "项目讨论组"
}
```

#### 发送群文本消息

```json
{
  "group_id": 90000001,
  "content": "今晚把接口联调一下"
}
```

#### 推送给成员的群消息

```json
{
  "msg_id": 193000000001,
  "group_id": 90000001,
  "sender_uid": 1001,
  "msg_type": 0,
  "content": "今晚把接口联调一下",
  "timestamp": 1742810000
}
```

---

## 8. 核心业务时序

### 8.1 建群流程

1. 客户端发送 `ID_CREATE_GROUP_REQ`
2. `ChatLogicSystem` 解析 DTO，并从 `session` 获取真实 `uid`
3. `GroupService` 校验群名和成员列表
4. 事务内完成：
5. 写 `chat_group`
6. 写群主和成员到 `chat_group_member`
7. 给所有成员初始化 `group_read_state`
8. 给建群者返回 `group_id`
9. 给在线成员推送 `ID_GROUP_EVENT_NOTIFY`

### 8.2 发送群消息流程

1. 客户端发送 `ID_GROUP_TEXT_MSG`
2. `ChatLogicSystem` 调用 `GroupMessageService`
3. 校验发送者是否为群成员
4. 校验群状态是否正常
5. 校验是否被禁言
6. 事务内完成：
7. 写 `group_message_content`
8. 查当前有效成员列表
9. 给每个成员写 `group_user_timeline`
10. 更新 `user_conversation`
11. 事务提交后：
12. 对在线成员直接推送
13. 对跨服在线成员走 gRPC
14. 离线成员等待下次同步拉取

### 8.3 群消息同步流程

1. 客户端携带 `last_msg_id` 请求 `ID_GROUP_SYNC_REQ`
2. 服务端查询 `group_user_timeline where owner_uid=? and msg_id>?`
3. 批量回表查询 `group_message_content`
4. 按 `msg_id asc` 返回给客户端
5. 客户端写入本地 SQLite

### 8.4 退群流程

1. 客户端发送 `ID_QUIT_GROUP_REQ`
2. `GroupMemberService` 校验身份
3. 更新 `chat_group_member.status = quit`
4. 写一条 `msg_type=9` 的系统消息
5. 在线成员收到群事件通知

---

## 9. 跨服设计建议

你当前项目已经有“按 uid 找所在 ChatServer”的路由模式。群聊不应推翻它，只需要在此基础上补一个群消息跨服投递 RPC。

建议新增：

- `DeliverGroupMessage`
- 或者更通用的 `DeliverUserMessage`

RPC 职责只做一件事：

- 把一条已确定目标用户的群消息，投递给目标用户所在的 `ChatServer`

不要让 gRPC 接口承担群成员计算。群成员列表、权限校验、正文落库都应在发起端 `ChatServer` 完成。

推荐流程：

1. 发起端 `ChatServer` 完成落库和成员计算
2. 对每个在线成员查 `uid_server:{uid}`
3. 本机成员直接推送
4. 异机成员走 `DeliverGroupMessage`

这样能保持群消息路由简单清晰。

---

## 10. 客户端推荐改造

客户端的第一优先级不是“加一个群列表页面”，而是把会话抽象升级掉。

### 10.1 新增 ConversationType 和 ConversationId

建议新增：

```cpp
enum class ConversationType : uint8_t {
    Peer = 1,
    Group = 2
};

struct ConversationId {
    ConversationType type = ConversationType::Peer;
    qint64 id = 0;

    bool operator<(const ConversationId& rhs) const {
        if (type != rhs.type) return type < rhs.type;
        return id < rhs.id;
    }
};
```

### 10.2 MessageInfo 结构升级

当前 `MessageInfo` 需要扩展为：

```cpp
struct MessageInfo {
    qint64    msgId = 0;
    qint64    conversationId = 0;
    int       conversationType = 1;
    int       fromUid = 0;
    int       toUid = 0;
    QString   content;
    QDateTime time;
    bool      isSelf = false;
    enum Type { Text, Image, File, System } type = Text;
};
```

建议保留 `fromUid/toUid`，因为渲染群消息时仍需要知道发送者是谁。

### 10.3 ChatMgr 改造方向

`client/Manager/ChatMgr.h` 当前以 `peerUid` 为 key，这里必须改成：

- 历史消息按 `ConversationId`
- 未读数按 `ConversationId`
- 当前活动会话按 `ConversationId`

不要继续假设“一个会话只对应一个对方 uid”。

### 10.4 ChatStorage 改造方向

`client/Utils/ChatStorage.cpp` 当前本地表建议从：

- `peer_uid`

升级成：

- `conversation_type`
- `conversation_id`
- `sender_uid`
- `peer_uid` 或 `target_uid`

推荐 SQLite 新结构：

```sql
CREATE TABLE messages (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_type  INTEGER NOT NULL,
    conversation_id    INTEGER NOT NULL,
    from_uid           INTEGER NOT NULL,
    to_uid             INTEGER NOT NULL,
    content            TEXT NOT NULL,
    timestamp          INTEGER NOT NULL,
    is_self            INTEGER NOT NULL,
    type               INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_conv_time
ON messages(conversation_type, conversation_id, timestamp);
```

### 10.5 TcpMgr 改造方向

`client/Network/tcpmgr.cpp` 需要新增：

- `ID_GROUP_TEXT_MSG` handler
- `ID_GROUP_EVENT_NOTIFY` handler
- `ID_GROUP_SYNC_RSP` handler

并且文本消息接收后，不再默认丢给单聊逻辑，而是根据消息类型分发到：

- 单聊会话
- 群会话

### 10.6 UI 改造方向

建议：

- 聊天窗口继续复用现有 `chatDialog`
- 顶部信息区新增群名、群成员数、群设置入口
- 消息气泡左侧显示群成员昵称
- 会话列表增加群头像和群名称

第一版不必追求复杂群资料页，先把消息链路跑通。

---

## 11. 推荐的落地顺序

### Phase 1: 数据和服务端最小闭环

- 新增群表
- 新增 `GroupService`
- 新增 `GroupMemberService`
- 新增 `GroupMessageService`
- 打通建群、查群资料、发群文本消息、群消息同步

### Phase 2: 客户端会话抽象升级

- 引入 `ConversationId`
- 改造 `MessageInfo`
- 改造 `ChatMgr`
- 改造 `ChatStorage`
- 增加群消息接收和群会话列表展示

### Phase 3: 群成员管理能力

- 邀请成员
- 退群
- 踢人
- 群系统消息
- 群公告

### Phase 4: 高级能力

- 管理员
- 禁言
- @ 提醒
- 群文件
- 会话免打扰
- 已读位置

---

## 12. 需要避免的设计

### 12.1 不要把群聊实现成 N 条单聊正文

否则会带来：

- 存储膨胀
- 同步复杂
- 撤回和系统消息困难

### 12.2 不要把群成员存在一个 JSON 字段里

短期快，长期一定会出现：

- 成员查询慢
- 权限控制差
- 踢人和禁言难做
- 索引不可用

### 12.3 不要继续让 ChatLogicSystem 成为“超大业务文件”

群聊如果继续往 `ChatLogicSystem.cpp` 塞逻辑，后续会比单聊更难维护。

### 12.4 不要一开始就把单聊和群聊同步层完全重构成一个通用超大模型

第一版先并行实现，群聊跑稳后再统一抽象，风险更低。

---

## 13. 建议的第一批代码落点

第一批建议新增或修改的文件如下。

### 服务端

新增：

- `ChatServer/DTO/GroupDTO.h`
- `ChatServer/Service/GroupService.h`
- `ChatServer/Service/GroupService.cpp`
- `ChatServer/Service/GroupMemberService.h`
- `ChatServer/Service/GroupMemberService.cpp`
- `ChatServer/Service/GroupMessageService.h`
- `ChatServer/Service/GroupMessageService.cpp`

修改：

- `ChatServer/const.h`
- `ChatServer/ChatLogicSystem.cpp`
- `ChatServer/MySqlPool.h`
- `ChatServer/MySqlPool.cpp`
- `ChatServer/message.proto`
- `ChatServer/ChatServiceImpl.h`
- `ChatServer/ChatServiceImpl.cpp`
- `sql/optimized_schema.sql`

### 客户端

新增：

- `client/Model/ConversationId.h`
- `client/DTO/GroupMessageDTO.h`

修改：

- `client/Utils/global.h`
- `client/Model/MessageInfo.h`
- `client/Manager/ChatMgr.h`
- `client/Manager/ChatMgr.cpp`
- `client/Utils/ChatStorage.h`
- `client/Utils/ChatStorage.cpp`
- `client/Network/tcpmgr.cpp`
- `client/UI/chatdialog.cpp`

---

## 14. 最终建议

对当前项目来说，最稳妥的群聊架构不是“照搬别人的 IM 项目”，而是：

- 保留你现有的多服务和跨服路由结构
- 把群聊正式抽象成 `GroupService + GroupMemberService + GroupMessageService`
- 客户端把会话模型从 `peerUid` 升级成 `ConversationId`
- 群消息继续沿用“正文一份，成员 Timeline 扩散”的思路

这样做的结果是：

- 第一版群聊能尽快上线
- 不会破坏已有单聊、文件、WebRTC 逻辑
- 后续能自然扩展到管理员、公告、禁言、@、群文件和统一会话中心

