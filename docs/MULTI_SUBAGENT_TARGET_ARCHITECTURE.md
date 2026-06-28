# Daima Agent 多 Subagent 目标架构

本文档不是变更流水账，而是当前代码基线下的目标架构说明。

目标只有一个：

- 参考 `github/oh-my-openagent` 的 background-agent 编排语义
- 参考 `github/opencode` 的 session-first reducer / transport 结构
- 让 `daima-agent` 最终稳定、可恢复、可观察、可并发地支持多个 subagent

---

## 1. 当前代码已经到哪里

基于当前仓库代码，`daima-agent` 已经不再是“单回合里临时插几条子任务日志”，而是基本形成了四层后端 + 三层前端。

### 1.1 后端四层

1. `turn / delegate entry`

- `kernel/turn/turn_entry.c`
- `kernel/turn/turn_gate.c`
- `drivers/tool/tool_delegate.c`

职责：

- 接收父消息
- 判断是否走 `!test` / interview / delegate
- 进入单任务或 batch/coordinator 路径

2. `coordinator + task store`

- `kernel/tooling/delegate/delegate_task_store.c`
- `kernel/tooling/delegate/delegate_task_runtime.c`
- `kernel/tooling/delegate/delegate_task_projection.c`

职责：

- 保存 `delegate_task_record_t`
- 保存 `delegate_coordinator_record_t`
- 维护 `visible_revision / wake_state / pending_request`
- 维护 child task 与 coordinator 的聚合状态

3. `parent wake + transport flush`

- `kernel/tooling/delegate/delegate_parent_wake.c`
- `arch/host/ws_server_host.c`
- `drivers/channel/gateway/ws_http_helpers.c`

职责：

- drain changed coordinator
- defer / retry / dedupe parent wake
- websocket / http 向前端投影 coordinator 与 child session
- `!test` 自检时先预检 `~/.agent-data/spiffs_data/workspace/opencode`
- 若缺失则自动 clone `https://github.com/sst/opencode.git`
- self-test follow-up 结束后由后端按 marker 自动 probe `agent.log`，把多 subagent 日志判定重新推回前端

4. `session / projection export`

- `kernel/tooling/delegate/delegate_session_json.c`
- `kernel/tooling/delegate/delegate_state_json.c`

职责：

- 导出 child session snapshot
- 导出 coordinator snapshot
- 为 HTTP restore / websocket 补帧 / parent merge 提供统一 projection

### 1.2 前端三层

1. `transport / runtime shell`

- `spiffs_data/web/subagent-transport.js`
- `spiffs_data/web/subagent-chat-transport.js`
- `spiffs_data/web/session-restore.js`
- `spiffs_data/web/subagent-runtime.js`

职责：

- websocket / http restore
- 统一 session payload 获取、history-only 读取、snapshot replay、delta recovery
- 单一 runtime state 容器

2. `reducer / selector / bridge`

- `spiffs_data/web/subagent-state-reducer.js`
- `spiffs_data/web/subagent-state-selectors.js`
- `spiffs_data/web/subagent-app-bridge.js`

职责：

- 把 transport 输入变成统一状态
- 把状态变成 coordinator / session / blocker / detail view model

3. `page shell / controller / view`

- `spiffs_data/web/app.js`
- `spiffs_data/web/subagent-panel-controller.js`
- `spiffs_data/web/subagent-detail-view.js`
- `spiffs_data/web/subagent-coordinator-controller.js`

职责：

- 页面交互
- DOM 绑定
- 面板切换

结论：

- 当前项目已经具备“多个 subagent 并发启动、child session 保留、web 端恢复展示、parent wake 集中 flush”这些关键骨架
- 现在的真实问题不是“能不能多 subagent”
- 真实问题是“最终真相源还没完全统一，交互模型还没彻底 session-first”

---

## 2. 参考实现真正强在哪里

### 2.1 `oh-my-openagent` 的强项

核心参考：

- `packages/omo-opencode/src/features/background-agent/session-idle-event-handler.ts`
- `packages/omo-opencode/src/features/background-agent/parent-wake-prompt-dispatch.ts`

它的关键不是“会调多个子 agent”，而是：

- 子任务完成与父会话唤醒彻底解耦
- `idle` 不等于立刻完成
- 父会话唤醒要经过：
  - defer
  - retry
  - dedupe
  - no-reply admit
  - history-gated suppression
- “父会话刚刚有普通活动” 与 “父会话已经真正消费了这次 wake” 是两回事

这也是当前 `daima-agent` 里 `delegate_parent_wake.c` 已经最接近，但还没完全追平的部分。

### 2.2 `opencode` 的强项

核心参考：

- `packages/opencode/src/cli/cmd/run/session-data.ts`
- `packages/opencode/src/cli/cmd/run/subagent-data.ts`
- `packages/opencode/src/cli/cmd/run/stream.transport.ts`
- `packages/sdk-next/README.md` 中的 `sessions.events({ sessionID, after })`

它真正强的地方是：

- `session-first`
- reducer 纯处理事件与状态
- transport 只负责订阅和转发
- footer / blocker / scrollback / subagent detail 都围绕同一份 session data 推进
- `sessions.events(after)` 提供 durable replay cursor，而不是“若干临时 websocket 事件 + 一份快照兜底”

因此 `opencode` 的重点不是 UI 样式，而是：

- durable event stream
- session reducer
- selector 驱动 UI

---

## 3. 当前 `daima-agent` 与目标架构的真实差距

当前差距可以压缩成三条主线。

### 3.1 真相源还不够统一

当前后端同时存在这些“看起来都像结果”的来源：

- `task.output`
- agent summary / coordinator summary
- `child_session.latest_frame`
- `child_session.history`

虽然现在已经越来越偏向 `child_session-first`，但还没有完全做到：

- 对用户可见的任何 child 结果，都优先来自 `child_session`
- `task.output` 只保留为 raw protocol data 或兜底

这会导致：

- parent merge、HTTP restore、web detail、coordinator summary 仍可能读到不同来源

### 3.2 replay 模型还不是 durable stream

当前已经有：

- `visible_revision`
- child session cursor/window
- `subagent_state_delta` / `subagent_state_deltas`

但本质仍偏 snapshot-window：

- 更像“当前截一段最近窗口”
- 还不是 `opencode sessions.events(after)` 那种 durable replay stream

这会限制：

- reconnect 后的强一致恢复
- 长会话多子任务滚动浏览
- 更复杂 blocker / question / permission 聚合

### 3.3 Web 仍偏 coordinator-first

虽然前端已经有：

- `subagent-session-rail`
- detail panel
- reducer/selectors

但现在一级交互对象仍明显偏向：

- “我有一个 coordinator 正在跑”

而不是：

- “我有多个后台子会话，各自有状态、blocker、frames、commits”

这说明前端结构已经开始向 `opencode` 方向靠，但用户心智还没完全切过去。

### 3.4 当前已经补上的 session-first 边界

这轮代码基线下，有三个之前容易串状态的边界已经收紧：

1. history-only fetch 与 snapshot restore 已分离

- `spiffs_data/web/subagent-transport.js`
- `spiffs_data/web/session-state-runtime.js`

现在“为了 reconcile 历史消息而读取 unified session 数据”不会再顺手重放 subagent snapshot。

也就是说：

- 读 history 只读 history
- restore snapshot 才会改 runtime subagent state

这一步很关键，因为它避免了：

- assistant 回复刚到时触发 history reconcile
- reconcile 再把当前活跃 chat 的 subagent live state 用旧 snapshot 覆盖掉

2. 显式 session switch 可以强制应用目标 snapshot

- `spiffs_data/web/app.js`
- `spiffs_data/web/session-restore.js`
- `spiffs_data/web/subagent-transport.js`

当前已经明确区分两类 restore：

- 被动/live 恢复：
  - 仍然受 stale snapshot guard 保护
- 用户显式切会话：
  - 允许 `forceApplySnapshot`
  - 目标 chat 的 snapshot 会成为新的真相源

这一步避免了“为了保护当前 live chat，不小心把显式切会话也一起拦住”。

3. snapshot hydrate 不再继承上一 chat 的 subagent 运行态

- `spiffs_data/web/subagent-state-reducer.js`

现在非空 snapshot restore 会从空状态重建，而不是继承前一个 chat 的：

- coordinators
- details
- eventLog
- blockerLog
- interactive 状态

这避免了跨 chat 污染，符合 `opencode` 那种 session-first 浏览模型：切到哪个 session，就以哪个 session 的快照和 replay 为准。

---

## 4. 最终目标架构

### 4.1 Coordinator 只负责 orchestration

最终 `coordinator` 应只保留这些职责：

- 这一批任务如何拆
- 哪些任务依赖哪些任务
- 这批任务生命周期处于 queued/running/done/failed 的哪个阶段
- 父会话什么时候应该被 resume

`coordinator` 不应该再承担：

- 用户细节展示真相
- blocker 真相
- child timeline 真相

换句话说：

- `coordinator` 是 orchestration object
- `child_session` 才是 user-facing session object

### 4.2 Child session 成为唯一展示主语

最终所有用户可见的 child 结果都应优先从 `child_session` 派生：

- parent merge
- coordinator card summary
- subagent detail panel
- http restore
- websocket replay

优先级应该固定为：

1. `child_session` 的最新可视 frame / rendered output
2. `child_session.history / commits`
3. 仅在上面都没有时才回退 `task.output`

这样才能保证：

- live 看见什么
- refresh 后看见什么
- parent merge 说什么

这三件事来自同一份事实源。

### 4.3 Parent wake 成为唯一 resume admission 中心

最终父会话 resume 的所有判定都应继续收口到 `delegate_parent_wake.c`：

- 是否 defer
- 是否 retry
- 是否 dedupe
- 是否 no-reply admit
- 是否因 parent recent activity 暂缓
- 是否因 parent 已消费这次 wake 而丢弃 retained resume

任何 worker、turn 层、web 层都不应该直接决定父会话 resume。

### 4.4 Web 改为 session-first 浏览模型

最终前端的一级交互对象应是：

- parent session
- child session list
- selected child session detail
- 当前 blocker / pending request

`coordinator` 退化成：

- 背后这批子任务属于哪一组
- 这组有没有依赖关系
- 这组是否已经整体结束

前端心智应从：

- “查看编排面板”

收敛到：

- “查看子会话列表与当前阻塞项”

### 4.5 Transport -> reducer -> selector -> view 成为固定边界

最终应该稳定保持这条边界：

1. transport
   - websocket/http 输入
   - 只负责接收与恢复

2. reducer
   - 吸收 coordinator/session/blocker/delta
   - 只负责状态推进

3. selector
   - 派生 tab/detail/blocker/summary view model

4. view/controller
   - 只消费 selector 结果
   - 不自己维护第二份业务真相

这正是 `opencode stream.transport -> session-data/subagent-data -> footer/detail` 的核心模式。

当前还差的不是“有没有这条边界”，而是边界内部的 replay 语义还不够彻底：

- `subagent-transport.js` 里虽然已经开始拆分
  - `fetchUnifiedSessionPayload`
  - `replayUnifiedSessionState`
  - `fetchUnifiedSessionHistory`
- 但 durable replay 仍然依赖 snapshot + delta recovery 的混合路径
- 还不是 `opencode stream.transport` 那种由单一 durable event stream 驱动的最终形态

---

## 5. 下一阶段最值得做的顺序

如果目标是“完美支持多 subagent”，不应该平均用力，而应该按下面顺序推进。

### 阶段 A：彻底收口 child session canonical source

目标：

- 任何 child 结果展示优先读 `child_session`
- `task.output` 退为 raw/兜底

主要文件：

- `kernel/tooling/delegate/delegate_session_json.c`
- `kernel/tooling/delegate/delegate_state_json.c`
- `kernel/turn/turn_entry.c`
- `drivers/tool/tool_delegate_summary.c`

验收标准：

- parent merge / coordinator summary / HTTP restore / websocket detail 文本来源一致

### 阶段 B：补强 parent wake 的 history-gated admission

目标：

- 更接近 `oh-my-openagent`
- 明确区分：
  - parent 有普通活动
  - parent 已消费这次 wake

主要文件：

- `kernel/tooling/delegate/delegate_parent_wake.c`
- `kernel/turn/turn_context.c`
- `kernel/loop.c`

验收标准：

- retained resume 在 busy parent / no-reply / failed-reply-required 场景下行为稳定一致

### 阶段 C：把前端彻底推到 session-first

目标：

- child session list/detail/blocker 成为一级 UI
- coordinator 面板退为辅助视图

主要文件：

- `spiffs_data/web/subagent-state-selectors.js`
- `spiffs_data/web/subagent-app-bridge.js`
- `spiffs_data/web/app.js`
- `spiffs_data/web/index.html`

验收标准：

- 用户即使不打开“编排”视图，也能直接理解当前有哪些子会话、谁在阻塞、谁已完成

### 阶段 D：把 replay 继续往 durable stream 靠

目标：

- 让 restore/reconnect 更接近 `sessions.events(after)` 的消费方式
- 继续把 `payload fetch`、`history-only fetch`、`snapshot replay`、`delta recovery` 明确拆成独立边界

主要文件：

- `delegate_session_json.c`
- `delegate_state_json.c`
- `subagent-transport.js`
- `session-restore.js`

验收标准：

- reconnect / refresh / incremental delta 使用统一 cursor 语义，而不是多套“猜测式补偿”
- history reconcile 不会再对 live subagent state 产生副作用
- 显式 session switch 与被动 live recovery 的 snapshot apply 语义清晰分离

---

## 6. 当前阶段的明确判断

基于当前代码和最新自检结果，可以明确下结论：

- 项目已经支持真实多 subagent 调度，不是纸面能力
- `!test` 已经能基于 `~/.agent-data/spiffs_data/workspace/opencode` 自动 clone、分析并回看日志验证调度
- `!test` 的日志判定不再只依赖模型自己读日志，后端已经能在 follow-up 后自动 probe `agent.log` 并推送更新后的 `self_test_result`
- `build-kbuild/agent --self-test` 当前已达到 `205/205 passed`

但如果目标是“完美支持多 subagent”，当前还不能说已经完成。

原因不是功能缺失，而是架构终态还差最后三步：

- `child_session` 还没有成为唯一展示真相源
- replay 还不是 durable stream
- web 还没有彻底 session-first

这三步做完，项目才算真正对齐 `oh-my-openagent + opencode` 的成熟多 subagent 模型。
