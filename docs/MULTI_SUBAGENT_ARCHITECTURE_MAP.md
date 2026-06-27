# Daima Agent 多 Subagent 架构地图

这份文档是 `docs/MULTI_SUBAGENT_ARCHITECTURE_GAP.md` 的压缩版执行地图，只回答四个问题：

1. 当前代码已经分成了哪些层
2. 每一层在 `daima-agent` 里的真实入口在哪里
3. 与 `oh-my-openagent` / `opencode` 对齐到了哪里
4. 想走到“稳定支持多 subagent”最终态，下一步应该按什么顺序推进

---

## 1. 当前总分层

当前代码已经可以按五层来理解：

1. `turn / orchestration entry`
2. `coordinator + child session state`
3. `parent wake + transport projection`
4. `web reducer + session projection`
5. `chat shell / coordinator UI`

这五层已经比早期“主回合 + tool append 文本日志”清晰很多，说明项目已经进入可演进状态，不适合再按临时 patch 思路继续堆。

---

## 2. `daima-agent` 当前代码边界

### 2.1 Turn / Orchestration Entry

这一层决定“什么时候进入多 subagent 模式”。

核心文件：

- `kernel/loop.c`
- `kernel/turn/turn_entry.c`
- `kernel/turn/turn_prepare.c`
- `kernel/turn/turn_pipeline.c`
- `kernel/turn/turn_finish.c`
- `drivers/tool/tool_delegate.c`
- `kernel/turn/turn_interview.c`

当前职责：

- 接收父消息
- 决定是否进入 interview / clarify
- 执行 `delegate_task`
- 生成单任务或 batch/coordinator
- 在 parent resume 时把 delegate 结果重新注回主回合

对标结论：

- 这一层已经有点像 `oh-my-openagent` 的 “planner output -> orchestrator entry”
- 但仍然是 `turn-first`，不是 `session-first`
- 现在的入口足够用，不应该再把核心复杂度继续堆回 `turn_entry.c`

### 2.2 Coordinator + Child Session State

这一层是当前真正的事实中心。

核心文件：

- `kernel/tooling/delegate/delegate_task_store.c`
- `kernel/tooling/delegate/delegate_task_runtime.c`
- `kernel/tooling/delegate/delegate_task_projection.c`
- `kernel/tooling/delegate/delegate_session_json.c`
- `kernel/tooling/delegate/delegate_state_json.c`
- `kernel/tooling/delegate/delegate_task_query.c`

当前职责：

- 保存 `task record`
- 保存 `coordinator record`
- 保存 `visible_revision`
- 保存 child runtime 对应的 `child_session`
- 维护 `history / frames / commits / pending_queue`
- 为 HTTP / WS 提供统一 JSON projection

对标结论：

- 这是当前最接近 `opencode session model` 的部分
- 已经从“状态散落在 worker / websocket / UI”进化为“先入 store，再做 projection”
- 但 `child_session` 现在仍更像窗口化 snapshot，不是 durable event stream

### 2.3 Parent Wake + Transport Projection

这一层负责把 store 的变化投递给父会话和 Web。

核心文件：

- `kernel/tooling/delegate/delegate_parent_wake.c`
- `arch/host/ws_server_host.c`
- `drivers/channel/gateway/ws_client.c`
- `drivers/channel/gateway/ws_http_helpers.c`
- `kernel/channel/channel_runtime.c`

当前职责：

- drain changed coordinators
- 发 websocket `coordinator_status / coordinator_output / coordinator_done`
- 发 websocket `subagent_start / progress / blocked / done / subagent_session`
- 处理 parent resume gate
- 暴露 `/api/subagent_state`
- 暴露 `/api/subagent_state_delta`
- 暴露 `/api/subagent_state_deltas`
- 暴露 `/api/subagent_state_delta_chat`

对标结论：

- 已经明显向 `oh-my-openagent background wake` 靠拢
- `visible_revision` 已经开始承担 replay handoff cursor 的角色
- 但现在还是“事件类型 + snapshot delta”的混合体，不是 `opencode sessions.events(after)` 那种统一 durable stream

### 2.4 Web Reducer + Session Projection

这一层已经不是简单 DOM 拼接，而是有了明确状态机。

核心文件：

- `spiffs_data/web/subagent-state-core.js`
- `spiffs_data/web/subagent-state-reducer.js`
- `spiffs_data/web/subagent-state-selectors.js`
- `spiffs_data/web/subagent-state.js`
- `spiffs_data/web/subagent-runtime.js`
- `spiffs_data/web/subagent-event-adapter.js`
- `spiffs_data/web/subagent-ui-orchestrator.js`
- `spiffs_data/web/subagent-transport.js`
- `spiffs_data/web/subagent-chat-transport.js`
- `spiffs_data/web/subagent-bootstrap.js`

当前职责：

- websocket/http 输入统一 parse
- `subagent_event / subagent_session / coordinator` reducer
- snapshot hydrate
- delta recovery
- `replay_reset`
- stale payload 防回退
- blocker / pending_queue / detail timeline / commits / history 聚合

对标结论：

- 这部分已经明显开始像 `opencode subagent-data reducer`
- 当前最大进步不是“拆了很多 js 文件”，而是：
  - live websocket 和 HTTP restore 已经共用一份 reducer 真相
  - child session detail 已经是一等对象
- 当前主要缺口是：
  - durable cursor 仍然偏弱
  - message history 和 subagent session 还是两套恢复链

### 2.5 Chat Shell / Coordinator UI

这一层现在已经不再是唯一真相来源。

核心文件：

- `spiffs_data/web/app.js`
- `spiffs_data/web/index.html`
- `spiffs_data/web/subagent-panel-controller.js`
- `spiffs_data/web/subagent-detail-view.js`
- `spiffs_data/web/subagent-coordinator-controller.js`
- `spiffs_data/web/subagent-coordinator-view.js`

当前职责：

- 顶层 chat shell
- websocket socket lifecycle
- session list
- coordinator panel
- subagent detail panel
- interactive prompt modal

对标结论：

- 当前 UI 已经有 session-first 雏形
- 但主交互仍然偏 coordinator-first
- 用户仍能明显感知到“有一批 coordinator 在跑”，而不是“我有多个后台子会话”

---

## 3. 对标 `oh-my-openagent` / `opencode` 的真实结论

### 3.1 已经补到位的部分

当前项目已经具备这些关键能力：

- 多个 subagent 可并发启动
- staged dependency 可见
- parent wake 是集中式 flush，不再由 worker 线程乱发
- child session 已有 `history / frames / commits / pending_queue`
- websocket 和 HTTP restore 已收敛到统一 reducer
- UI 已经可以看到多 subagent detail，而不是只能看到一行 tool log

这说明当前项目已经不是“不会多 subagent”，而是“还没完全进入最终态架构”。

### 3.2 还没有补到位的核心缺口

真正的缺口只剩三条主线：

1. `child_session` 还不是 durable session stream
2. parent chat history 与 subagent session 仍是两套恢复链
3. UI 还没有彻底完成 `session-first` 交互收敛

更直白一点说：

- `coordinator` 现在既是编排对象，又还是半个展示中心
- `child_session` 已经很强，但还不是唯一协议中心
- websocket `visible_revision` 已经可用，但还不够当最终 durable cursor

---

## 4. 最终目标架构

如果以 `oh-my-openagent + opencode` 为上限，`daima-agent` 最终应该收敛为下面这套结构。

### 4.0 运行时边界先决条件

在继续推进多 subagent 最终态之前，运行时边界必须保持清晰：

- 开发实例：
  - `./run.sh`
  - 实际启动 `build-kbuild/agent`
- 安装实例：
  - `./install.sh`
  - 安装到 `~/.agent-data/bin/agent`
  - 当前安装脚本也会主动重启安装版并做 `/health` 检查

这不是运维细节，而是架构前提：

- 如果开发实例和安装实例同时占用同一个 `web_port`
- Web 页面的静态资源、websocket 连接、HTTP snapshot、runtime log 可能会指向不同进程
- 这样会直接污染对 multi-subagent 协议、replay 语义、session restore 的判断

所以后续所有多 subagent 验证都应该先明确：

- 当前验证的是开发实例
- 还是安装实例

### 4.1 Coordinator 只负责编排

`coordinator` 最终只保留这些职责：

- 这一批任务怎么拆
- 哪些任务有依赖
- 当前 batch 生命周期到哪里
- 父会话什么时候可以 resume

它不应该再承担：

- 用户侧 detail 真相
- blocker 真相
- timeline 真相

### 4.2 Child Session 成为唯一交互对象

最终用户真正交互的对象应该是 `child_session`，而不是 `coordinator`。

`child_session` 应成为统一协议中心，包含：

- durable event seq
- visible message/history
- frames / commits
- pending permissions/questions
- terminal summary
- replay cursor

### 4.3 HTTP / WS 共用一条 durable replay 语义

最终不应该长期保留：

- 一套 websocket live event 语义
- 一套 HTTP snapshot delta 语义

应该收敛成：

- durable replay stream
- live tail 只是 replay 的继续
- ephemeral UI fragment 明确不推进 durable cursor

这一步是最接近 `opencode session/events(after)` 的关键。

### 4.4 Web 改成 session-first，coordinator 退为辅视图

最终 UI 应该是：

- 用户主要看到多个后台 session
- 每个 session 有自己的 detail / blockers / history
- coordinator 只在查看编排关系、依赖和排障时出现

也就是：

- `coordinator` 是 orchestration debug view
- `child_session` 是 product surface

---

## 5. 下一阶段正确顺序

当前最优顺序不是继续随机修问题，而是按下面四阶段推进。

### 阶段 1：统一 durable cursor

目标：

- 把 `visible_revision` 继续推进成更明确的 durable replay cursor
- 区分 durable event 和 ephemeral fragment
- 让 HTTP restore 和 websocket tail 真正做到一条链

优先看：

- `kernel/tooling/delegate/delegate_state_json.c`
- `kernel/tooling/delegate/delegate_parent_wake.c`
- `spiffs_data/web/subagent-transport.js`
- `spiffs_data/web/subagent-chat-transport.js`

### 阶段 2：把 child session 从 snapshot 推到 stream

目标：

- `child_session` 不再只是最近窗口快照
- 明确 after-seq / replay-reset / durable append contract
- 让 detail/history/commits 都能稳定 replay

优先看：

- `kernel/tooling/delegate/delegate_session_json.c`
- `kernel/tooling/delegate/delegate_task_projection.c`
- `kernel/tooling/delegate/delegate_task_store.c`

### 阶段 3：统一 parent history 与 subagent history 恢复

目标：

- 父会话 message history 和 subagent detail 不再分裂恢复
- 避免“主消息刷新后才出现、子任务已在另一个链路里恢复”
- 形成一个真正的会话恢复中心

优先看：

- `drivers/channel/gateway/ws_http_helpers.c`
- `drivers/memory/session_store*.c`
- `spiffs_data/web/app.js`
- `spiffs_data/web/subagent-bootstrap.js`

### 阶段 4：Web 完成 session-first 收敛

目标：

- coordinator panel 退为辅助视图
- 默认视图围绕 child session
- 用户不需要理解 coordinator 也能正常使用多 subagent

优先看：

- `spiffs_data/web/subagent-state-selectors.js`
- `spiffs_data/web/subagent-detail-view.js`
- `spiffs_data/web/subagent-coordinator-view.js`
- `spiffs_data/web/app.js`

---

## 6. 当前最关键的一句结论

`daima-agent` 现在最不该做的事，是继续把多 subagent 当成“delegate_task 调度成功 + 页面能看到几个卡片”。

下一阶段真正要做的是：

- 让 `child_session` 成为单一事实来源
- 让 HTTP / WebSocket 共享一条 durable replay 语义
- 让 UI 真正变成 session-first

只有这三件事做完，项目才会真正接近 `oh-my-openagent + opencode` 那种“稳定、可恢复、可并发、可观察”的多 subagent 最终态。
