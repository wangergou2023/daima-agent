# Daima Agent 多 Subagent 架构梳理与对标结论

本文档只回答三件事：

1. `daima-agent` 当前多 subagent 链路是怎么工作的
2. 与 `github/oh-my-openagent` / `github/opencode` 相比，已经补到了哪里
3. 还差哪些关键能力，下一步应该继续补什么

当前阶段结论先写在前面：

- `daima-agent` 的多 subagent 能力已经越过“能不能调起多个 child”这道坎
- 当前最关键的主线，不再是继续把 Web 页面拆更多文件，也不只是继续堆后台并发
- 下一阶段应明确走 `session-first` 收敛：
  - child session 要成为一等协议源
  - structured result 要优先于文本兜底
  - parent 汇总、HTTP 恢复、WebSocket 增量都应围绕同一份 child session / coordinator projection
  - `local overview` / `dependency merge` 这类 shortcut 只能作为受控优化，不能继续污染最终 child result 的语义层
  - live websocket 事件允许保留 raw structured `output` 作为 canonical protocol data，但 UI 展示必须统一消费渲染后的可视语义层，不能再把 raw JSON 当展示文本

代码基线以当前仓库为准：

- `kernel/tooling/delegate/delegate_task_store.c`
- `kernel/tooling/delegate/delegate_parent_wake.c`
- `kernel/turn/turn_entry.c`
- `drivers/tool/tool_delegate.c`
- `spiffs_data/web/app.js`

参考实现基线：

- `github/opencode/packages/opencode/src/cli/cmd/run/subagent-data.ts`
- `github/opencode/packages/opencode/src/cli/cmd/run/stream.transport.ts`
- `github/oh-my-openagent/packages/omo-opencode/src/features/background-agent/session-idle-event-handler.ts`
- `github/oh-my-openagent/packages/omo-opencode/src/features/background-agent/parent-wake-prompt-dispatch.ts`

---

## 1. 当前 `daima-agent` 的多 subagent 架构

### 1.1 主链入口

主回合仍然只有一条：

- `kernel/loop.c`
- `kernel/turn/turn_entry.c`

用户输入进入主链后，如果模型显式调用 `delegate_task`，会转入：

- `drivers/tool/tool_delegate.c`

这里负责两种模式：

- 同步单子任务
- 后台批量子任务，返回 `coordinator_id`

### 1.2 后台批量任务状态

后台批量任务的状态中心是：

- `kernel/tooling/delegate/delegate_task_store.c`

它维护两层对象：

- `delegate_task_record_t`
  - 单个子任务状态
  - `task_id / session_id / subagent_type / model / output / target_files`
- `delegate_coordinator_record_t`
  - 一批子任务的聚合状态
  - `coordinator_id / visible_revision / wake_state / completion_notified / parent_resume_enqueued`

这层已经不再是“只靠日志推断”，而是显式状态存储。

### 1.3 parent wake / websocket 推送

协调器状态变化不会直接在工具执行处散落推送，而是进入：

- `kernel/tooling/delegate/delegate_parent_wake.c`

这里承担三件事：

- 从 store drain `changed coordinator`
- 向 WebSocket 发送
  - `coordinator_status`
  - `coordinator_output`
  - `coordinator_done`
  - `subagent_progress`
- 在终态时向父会话注入 `MSG_SOURCE_DELEGATE`

这一步已经接近 `oh-my-openagent` 的“background task -> parent wake”思路。

补充说明（2026-06-27 最新）：

- `parent wake` 现在已经进一步收口成“主循环单点 flush”
  - background worker 完成子任务后，只负责：
    - 更新 `delegate_task_store`
    - 触发 staged downstream launch
  - background worker 不再直接调用 `agent_loop_poll_delegate_coordinators()`
- 统一 flush 点只保留在：
  - `kernel/loop.c`
  - `agent_loop_poll_delegate_coordinators()`
- 这次调整的目的不是代码风格，而是修真实并发问题：
  - 修复前，worker 线程和主循环线程都可能同时推进 `delegate_parent_wake_poll()`
  - 会出现同一 coordinator 在同一轮里被多次消费、旧快照和新快照交错处理
  - 最终表现为：
    - websocket 前端能看到 `coordinator_done`
    - 但终态 `parent resume` 可能永远不入父会话
- 修复后，`delegate_parent_wake_poll()` 也改成先冻结一轮 `pending wake snapshot` 再 flush
  - 不再边扫描 `s_pending` 边修改自己
  - 这让 `daima-agent` 的 parent wake 更接近 `oh-my-openagent` 的集中式 `flush runner + pending queue` 模型
- retained terminal resume 现在也进一步区分了两类语义：
  - 普通 `done` 态 retained resume，如果 defer 之后父会话已经继续产生新活动，会被判定为“已被父链消费”，不再重复注入 `MSG_SOURCE_DELEGATE`
  - `failed` 态 retained resume 不再走这条吞并分支，而是继续保留到父会话安全后再恢复
- 这一步直接对齐了 `oh-my-openagent` 里 `failure/reply-required wake must not be consumed by admit-only parent activity` 的核心约束

### 1.4 父会话结果合并

当前父会话不再依赖“再问一次模型做 merge”，而是在：

- `kernel/turn/turn_entry.c`

对 `MSG_SOURCE_DELEGATE` 走本地合并：

- 解析 `Coordinator snapshot`
- 本地拼出
  - `并行子任务汇总`
  - `状态`
  - `关键发现`
  - `建议继续看`
  - `原始子任务摘要`

这一步解决了之前“coordinator 完成但父会话说看不到子任务结果”的问题。

### 1.5 Web 侧展示

当前 Web 入口：

- `spiffs_data/web/index.html`
- `spiffs_data/web/app.js`

已经支持显示：

- `coordinator_status`
- `coordinator_output`
- `coordinator_done`
- `subagent_start`
- `subagent_progress`
- `subagent_done`

并且 coordinator 面板已经支持：

- 多 coordinator 并列显示
- 每个 agent 的状态、模型、耗时、输出展开
- 最近 subagent 事件时间线

这一层比之前“把 subagent 事件当 tool 文本 append”前进了一大步。

---

## 2. 与 `opencode` / `oh-my-openagent` 的对比

### 2.1 已经对齐的点

当前实现已经和参考实现对齐到这些层级：

- 有显式 coordinator store，而不是纯文本协议
- 有 parent wake 管理器，而不是子任务完成后直接乱发消息
- 有终态 completion / retry / wake_state
- 有前端 coordinator 可见面板
- 支持一批多个子任务并发启动
- 父会话能在 coordinator 结束后拿到合并结果

这说明后端生命周期已经从“能跑”进入“有状态机”的阶段。

补充进展（2026-06-28 本轮新增）：

- websocket `subagent_start / subagent_done / subagent_blocked` 事件现在已经显式区分：
  - `output`
    - 保留原始 structured output，作为协议 canonical data
  - `visible_output`
    - 给 Web/UI 使用的可视化文本
- Web reducer 现在优先消费 `visible_output`
  - `spiffs_data/web/subagent-state-core.js`
  - 这意味着 live event 路径不再需要从 raw JSON 猜测用户可见文本
- `delegate_parent_wake` 的 live replay 路径也已补齐同一语义：
  - coordinator flush 时发出的 `subagent_done/subagent_progress/subagent_blocked`
    会基于 result-json 渲染 visible text
  - 因此 live websocket 增量与 `child_session/coordinator snapshot` 的可视语义开始收敛
- `child_session.frames` 的中间 `subagent_step` 投影本轮也已继续修正：
  - `delegate_task_projection.c` 的 `session_record_step()`
  - `session_record_pending_request_frame()`
  不再把 shortcut/raw output 直接写进 frame `output_preview`
  - 对 `dependency_merge` 这类 structured result，现在 step frame 也会写入渲染后的可视文本
- `child_session` 现在还新增了窗口元信息：
  - `window.history_limit / history_count / history_total / history_truncated`
  - `window.frame_limit / frame_count / frame_total / frame_truncated`
  - `window.commit_limit / commit_count / commit_total / commit_truncated`
  - `window.history_first_seq / history_last_seq`
  - `window.frame_first_seq / frame_last_seq`
  - `window.commit_first_seq / commit_last_seq`
  - 这一步的意义不是“多几个数字”，而是让消费者第一次能明确知道：
    - 当前拿到的是完整 child session 视图
    - 还是被窗口裁剪后的最近片段
    - 以及当前窗口具体覆盖了哪一段 seq
  - 这比单纯给出 `history/frames/commits` 数组更接近 `opencode` 的 session replay 语义
- 当前这一层的结论是：
  - live event
  - child session summary
  - child session done frame
  - child session step frame
  已经基本对齐到同一可视语义层
  - 同时 child session 开始具备“窗口边界解释力”
  - 但它仍然是“最近窗口快照”，还不是完整 session stream

### 2.2 与 `opencode` 还存在的真实差距

`opencode` 的强项不只是“能看到多个 subagent”，而是：

- 独立的 `subagent-data reducer`
- `tabs + details + blockers + frames`
- 每个子会话自己的 scrollback / commit 历史
- footer 级选择和切换
- permission / question blocker 跨父子会话整合

这些点里，`daima-agent` 已经补上了一部分，不能再按旧结论判断：

- 已有每个 subagent 的 detail 面板
- 已有独立 reducer：
  - `spiffs_data/web/subagent-state.js`
- 已有子会话级 `timeline / commits / blockers / pending_queue`
- 已能消费后端 `child_session`
- 已能把 `permission/question` 收敛到统一 interactive blocker 容器
- `child_session` 协议已从只含 `summary + commits + pending_queue` 升级为显式带 `frames`
  - 后端：`delegate_parent_wake.c` 直接下发 `child_session.frames`
  - 前端：`subagent-state.js` 优先消费 `child_session.frames` 生成 detail timeline
  - 这让子会话详情不再完全依赖 `subagent_*` 事件侧推断，开始向 session-first 模型靠拢
- `child_session.cursor` 也已显式暴露 replay 元信息
  - `history / frames / commits` 三类窗口都带
    - `after_seq`
    - `visible_seq`
    - `first_visible_seq`
    - `next_seq`
    - `high_water_seq`
    - `has_more`
    - `replay_reset`
  - 语义上这已经不再是“当前数组长度”
  - 而是“子会话 durable seq 空间里，当前窗口覆盖到哪里、下一次应从哪里继续拉”
  - 这是当前 `daima-agent` 向 `opencode sessions.events(after)` 靠拢最关键的一层协议收敛
- `child_session.pending_queue` 已从“字符串摘要数组”升级为“结构化请求对象数组”
  - 每个条目至少带 `request_type / request_id / prompt`
  - 这样刷新恢复时，前端拿到的不是“像 blocker 的文本”，而是更接近 `opencode` session reducer 的结构化 blocker snapshot
- `/api/subagent_state` 已补上 parent session-first pending snapshot
  - 当 parent 先进入 `question_text` interview、coordinator 还没创建时，HTTP 不再返回 `404`
  - 顶层会直接返回 `pending_request`
  - 这样 Web 刷新或重连时，父级阻塞态可以先于 coordinator 恢复

现在的真实差距收敛为：

- `child_session` 虽然已经具备 `frames + commits + pending_queue`，但仍是“快照模型”，不是 `opencode` 那种持续滚动的完整 session stream
- `child_session` 虽然已经开始暴露 window metadata 和 seq boundary，让消费者知道当前是完整视图还是哪一段裁剪窗口，但还没有 replay cursor / after-seq / durable event tail 语义
- reducer 已拆分，但还没有像 `opencode` 那样彻底分成事件适配层、session reducer、footer reducer
- Web 详情视图仍以 coordinator 为中心，不是完全等价于 `opencode` 的 session-first 交互模型
- blocker/permission/question 虽然协议打通了，但 richer mixed-blocker 场景仍弱于 `opencode`
- live 事件与 child session 的“可视文本”虽然已收敛，但 protocol 侧仍是 `raw output + visible_output` 并存模式
  - 这符合当前工程现实，也比直接改写 raw `output` 更稳
  - 但如果后续要进一步对齐 `opencode` 的 session stream，最好再明确：
    - 哪些消费者可以读取 raw `output`
    - 哪些消费者只能读取 projection/selectors 派生的 visible text

补充进展（2026-06-27 本轮新增）：

- coordinator / child session 的 JSON 投影现在已经进一步统一到单独共享层：
  - `kernel/tooling/delegate/delegate_state_json.c`
  - 当前统一承接：
    - `coordinator_status`
    - `coordinator_done`
    - `subagent_session`
    - HTTP `/api/subagent_state`
- 这意味着：
  - `ws_http_helpers.c` 不再自己维护一套 parent/coordinator/agent/pending/blocker 拼接逻辑
  - `delegate_parent_wake.c` 也不再自己维护另一套 snapshot/completion/session payload 拼接逻辑
- 这一步直接减少了最危险的一类分叉：
  - HTTP 恢复态看到的 coordinator / pending_request / child_session
  - 与 WS 增量推送看到的 coordinator / pending_request / child_session
  字段定义和嵌套层级逐步统一
- 当前仓库已经新增对应回归：
  - `delegate parent subagent state json uses shared projection`

补充进展（2026-06-27 本轮新增）：

- Web 侧 `tabs` 已不再作为独立可写状态真相源
  - reducer 现在只写：
    - `details`
    - `coordinators`
    - `interactiveBlockers`
    - `selectedTabKey`
  - `visibleSubagentTabs()` 改为从 `details` 派生
- 这一步的价值不是“少一个 Map”：
  - 之前 `details` 和 `tabs` 在 reducer 中双写，天然存在排序、状态、blocker、更新时间漂移
  - 现在 tabs 退化为 selector 视图，更接近 `opencode` 的 reducer + selector 结构
  - 后续如果继续把 coordinator 面板和 footer 也收敛为派生视图，Web 侧就更容易真正转成 session-first

补充进展（2026-06-27 本轮新增）：

- Web 侧 `coordinators` 也进一步降成“元信息索引 + selector 派生 agents”
  - reducer 仍保留 coordinator 级元信息：
    - `coordinator_id`
    - `status`
    - `wake_state`
    - `team_run_id`
    - `dispatch_mode`
    - `blocker_kind / blocker_text`
  - 但不再长期把每个 agent 的完整副本存进 `state.coordinators`
  - `orderedCoordinatorStates()` 现在会从 `details` 按 `coordinator_id` 反向派生 coordinator 面板所需的 `agents`
- 这一步继续消掉一类典型漂移：
  - coordinator 卡片里的 agent 状态 / output / blocker
  - detail 面板里的 agent 状态 / output / blocker
  不再分别由两套状态写入路径长期持有
- 这让当前 Web 真相层进一步收敛为：
  - `details` 作为 subagent 级主状态
  - `coordinators` 作为批次/唤醒级元信息
  - `tabs` / coordinator `agents` 都通过 selector 派生

补充进展（2026-06-27 本轮新增）：

- Web 侧 detail 面板也继续从 `app.js` 手工拼装，收口到了 selector/view-model 路径
  - 新增：
    - `subagent-state-selectors.js -> detailPanelViewModel(...)`
  - 页面层 `app.js` 不再自己分别取：
    - `selectedSubagentDetailView`
    - `visibleSubagentTabs`
    - `orderedSubagentDetails().slice(0, 8)`
    - `effectiveSelectedSubagentKey`
    再手动拼成 detail render 输入
- 同时，`app.js` 已移除一批重复包装层：
  - `selectedSubagentDetail()`
  - `currentInteractiveRequest()`
  - `currentInteractiveValue()`
  - `applyInteractiveControllerState()`
  - 本地 `pendingQueuePrompt()`
  - 本地 `blockerForDetail()`
- 这一步的价值不是“少几个函数”：
  - 页面层不再同时承担 selector facade、interactive controller passthrough、detail render 输入装配
  - reducer/selectors/controller/view 的边界比之前更稳定
  - 多 subagent 并发时，detail 面板与 coordinator 面板读取的是同一批 selector 派生结果，更接近 `opencode` 的 `state -> selector -> footer/detail view` 模式
- 当前真实剩余差距进一步收敛为：
  - `app.js` 仍然是总 wiring 入口，尚未像 `opencode` 那样把 footer/detail/coordinator orchestration 完全拆到独立运行时模块
  - reducer action dispatch 仍由页面层触发，后续还可以继续向更明确的 event-runtime/controller 边界推进

补充进展（2026-06-27 本轮新增）：

- Web 侧新增了一个很小的 `subagent-runtime` 层：
  - `spiffs_data/web/subagent-runtime.js`
- 这层现在只负责三件事：
  - 持有 subagent UI 当前状态
  - 统一 `dispatch(action, helpers)`
  - 统一 `replaceSnapshot(snapshot, helpers)` / `select(selector, ...args)`
- `app.js` 已不再直接持有 `subagentUiState` 变量并手动做：
  - `reduceSubagentUiEventCore(...)`
  - `hydrateStateFromSnapshot(...)`
  - 再把结果散给各 selector
- 这一步虽然还不等于 `opencode` 的完整 stream runtime，但它已经建立了更接近参考实现的最小骨架：
  - 页面层负责 wiring
  - runtime 持状态
  - reducer 只负责纯状态推进
  - selector 负责派生 coordinator/detail/tabs/blockers
- 额外收益：
  - `check-subagent-web-ui.js` 现在也显式加载 `subagent-runtime.js`
  - 并且 `app.js` 保留 fallback runtime shim，避免脚本装载顺序或测试桩缺失时把整个 subagent UI 打空
- 这让后续继续对齐 `opencode` 时，可以更自然地往下面两步推进：
  - 把 websocket subagent event consumption 从 `app.js` 继续推到更明确的 event runtime
  - 把 coordinator/detail 刷新联动从页面脚本剥离成更稳定的 controller/runtime 协作边界

补充进展（2026-06-27 本轮新增）：

- Web 侧又新增了一层更贴近 `transport/orchestrator` 语义的组合层：
  - `spiffs_data/web/subagent-ui-orchestrator.js`
- 这层当前不持久化业务状态，也不替代 reducer；它只负责“组合动作”：
  - `applySnapshot(snapshot, helpers)`
  - `applyCoordinatorPayload(payload, helpers)`
  - `dismissInteractiveRequest(request, helpers)`
- 对应地，`app.js` 不再自己展开以下组合流程：
  - snapshot hydrate -> restore interactive blocker -> rerender panels
  - coordinator payload dispatch -> mark active -> rerender panels
  - interactive dismiss actions -> clear controller -> rerender detail
- 当前 Web 结构开始形成更清晰的三层：
  - `subagent-runtime`：持状态、dispatch、selector 访问
  - `subagent-ui-orchestrator`：处理组合状态迁移与多面板刷新联动
  - `app.js`：页面 wiring、socket/message 主入口
- 这一步离 `opencode` 的 `stream.transport -> reducers -> footer/session view` 还有差距，但方向已经更一致：
  - 页面层不再既持状态又展开复杂组合动作
  - runtime / orchestrator 逐步形成独立的演进面
- 当前最真实的剩余差距进一步收敛为：
  - websocket 消息主循环仍在 `app.js`，还没有完全下沉到独立 subagent transport/runtime
  - coordinator/detail 的刷新目前由 orchestrator 调用页面 render 入口，尚不是更彻底的订阅式 UI 更新

补充进展（2026-06-27 本轮新增）：

- Web 侧进一步新增了 transport 层：
  - `spiffs_data/web/subagent-transport.js`
- 当前 transport 已下沉两类能力：
  - websocket subagent payload 的 parse + adapter 转发
  - `/api/subagent_state` snapshot 拉取与 stale/404/empty 处理
- 对应地，`app.js` 已不再自己完整展开：
  - `raw websocket payload -> parse -> isSubagentPayload -> adapter.handle`
  - `fetch /api/subagent_state -> stale token 判断 -> 404/ok -> apply snapshot`
- 当前 Web 侧已形成更接近 `opencode` 的四层边界：
  - `subagent-runtime`：状态持有与 selector 访问
  - `subagent-ui-orchestrator`：组合动作与多面板刷新联动
  - `subagent-transport`：websocket/http 输入侧桥接
  - `app.js`：页面 wiring 与全局 chat shell 主循环
- 这一步的意义不在“文件变多”，而在于：
  - subagent 消息链路已经开始有独立演进面
  - 后续把 websocket 主循环里的 subagent 分支继续下沉时，不需要再从零拆一遍
  - 与 `opencode` 的 `stream.transport` 思路已经不再只是文档对标，而是代码结构上开始接近
- 当前最剩余的关键差距更新为：
  - websocket `onmessage` 主循环仍是统一 chat shell 入口，subagent transport 还不是独立订阅驱动
  - UI 刷新依旧由 orchestrator 主动触发，而不是 state 订阅/渲染调度模型

补充进展（2026-06-27 本轮新增）：

- `subagent-transport` 现在不再只处理“subagent 专属 payload”与 snapshot 拉取，还开始承接 websocket 消息分发：
  - `handleWebsocketMessage(raw, context)`
- 这一步已经把 `app.js` 里以下判断链下沉成 transport 行为：
  - `agent_state`
  - `session_sync`
  - `upload_done / upload_error`
  - `self_test_result`
  - `stopped`
  - `tool`
  - `pet_response`
  - `reasoning`
  - `response`
- 注意，这并不表示 chat shell 已经完全 transport 化；当前仍是：
  - `app.js` 提供这些 message handler 的具体实现
  - `subagent-transport` 负责把 websocket payload 路由到对应 handler
- 但结构上已经更接近 `opencode stream.transport` 的关键点：
  - websocket 输入的类型分发不再散落在页面脚本的 `onmessage` 大 if/else 中
  - transport 逐步成为输入侧的单一桥接点
- 当前剩余差距继续收敛为：
  - `app.js` 仍持有 handler 实现本身，transport 还不是完全自包含的 chat/subagent stream bridge
  - UI 更新仍由显式调用触发，而不是更进一步的 state-driven subscription/render scheduler

补充进展（2026-06-27 本轮新增）：

- `subagent-transport` 现在已经进一步吸收了 websocket handler 绑定本身：
  - transport 初始化时就绑定：
    - `handleAgentStateMessage`
    - `handleSessionSync`
    - `handleUploadDone`
    - `handleUploadError`
    - `handleSelfTestResult`
    - `handleStopped`
    - `handleToolMessage`
    - `handlePetResponse`
    - `handleReasoningMessage`
    - `handleAssistantResponse`
- 对应地，`app.js` 的 `ws.onmessage` 已基本退化为：
  - 解析原始 websocket payload
  - 调用 `transport.handleWebsocketMessage(raw)`
  - 如果 transport 没处理，再走剩余兜底路径
- 这一步的真实意义是：
  - websocket 类型路由和 handler 绑定都已经不再在页面层 inline 展开
  - `app.js` 开始更接近 “chat shell wiring + transport hook point”，而不是“巨大消息分发器”
- 仍然存在的真实剩余差距：
  - handler 实现体多数还在 `app.js`
  - transport 还没有进一步把这些 handler 拆成更小的 session/chat/subagent stream runtime 单元

补充进展（2026-06-27 本轮新增）：

- websocket message handler 的实现体现在也不再散落成一组全局函数：
  - `handleSessionSyncMessage`
  - `handleUploadDoneMessage`
  - `handleStoppedMessage`
  - `handleToolMessage`
  - `handleReasoningMessage`
  - `handleAssistantResponseMessage`
  这一批已经被收口成 `ensureSubagentTransport()` 内部的 `transportHandlers`
- 这不是纯粹的“挪代码”：
  - 这些 handler 本质上只服务 websocket 输入流
  - 把它们局部化到 transport 构造附近，能明显减少 `app.js` 继续扮演“消息处理仓库”
- 当前结构比上一轮更清楚：
  - `app.js` 提供少量页面级依赖和 wiring
  - `transportHandlers` 成为 websocket 输入行为的集中定义点
  - `subagent-transport` 负责 websocket/http 输入桥接和消息路由
- 当前真实剩余差距进一步收敛为：
  - handler 虽然已局部化，但仍闭包依赖页面层变量和 DOM helpers，尚未独立成更小的 chat transport/runtime 模块
  - `connect()` 仍然是页面层直接创建 websocket、绑定生命周期与心跳，transport 还没有接管 socket lifecycle

补充进展（2026-06-27 本轮新增）：

- `subagent-transport` 现在开始承接 websocket lifecycle handler 绑定：
  - 新增 `bindSocket(socket, handlers)`
- 当前 `connect()` 已不再直接写：
  - `ws.onopen = ...`
  - `ws.onclose = ...`
  - `ws.onerror = ...`
  - `ws.onmessage = ...`
  而是把这些 handler 通过 transport 统一绑定
- 这一步虽然还没有把 socket 创建、心跳、重连 timer 本身移出页面层，但已经形成了更清晰的职责切口：
  - 页面层负责创建 websocket 实例与本地 timer 资源
  - transport 负责把 websocket 事件路由到对应 lifecycle/message handlers
- 这让当前结构更接近 `opencode` 的 transport 角色：
  - transport 不只处理 payload parsing
  - 还开始负责“输入通道事件 -> handler”的绑定边界
- 当前剩余的主要差距进一步收敛为：
  - socket lifecycle 的资源管理（创建、重连、ping/stats timer）仍在 `app.js`
  - handler 实现仍闭包依赖页面层状态，transport 还不是完全独立的 runtime/service

补充进展（2026-06-27 本轮新增）：

- `subagent-transport` 现在已经接手 websocket lifecycle 里的 timer 资源管理：
  - `startPingLoop(...)`
  - `startStatsLoop(...)`
  - `scheduleReconnect(...)`
  - `clearRuntimeTimers()`
  - `clearReconnectTimer()`
- 对应地，页面层已经移除了对这些 timer 资源的直接持有：
  - `reconnectTimer`
  - `pingTimer`
  - `statsTimer`
- 现在的职责切口更明确：
  - `app.js`
    - 创建 websocket
    - 提供 reconnect/ping/stats 的具体动作回调
  - `subagent-transport`
    - 持有 timer 资源
    - 统一调度 ping/stats/reconnect 生命周期
    - 绑定 socket handlers
- 这一步使 transport 更像真正的“输入通道运行时”，而不只是一个 payload router
- 当前最主要的剩余差距进一步收敛为：
  - websocket 实例本身仍由页面层创建，transport 还没有完全接管 socket lifecycle
  - reconnect / ping / stats 的动作实现仍依赖页面层闭包，而不是 transport 内部服务化

补充进展（2026-06-27 本轮新增）：

- `subagent-transport` 现在新增了 `connectSocket(url, handlers)`：
  - transport 不再只绑定一个已存在的 websocket
  - 也开始承接 websocket 实例创建入口
- 对应地，`connect()` 当前已经退化为：
  - 计算 ws/wss URL
  - 调用 `transport.connectSocket(...)`
  - 仅保留 fallback 路径给缺少 transport 的极端情况
- 这一步意味着：
  - websocket 输入通道的创建、事件绑定、timer 调度已经大部分位于 transport 边界内
  - 页面层离“只保留 UI wiring”又近了一步
- 当前剩余差距继续收敛为：
  - open/close/reconnect/ping/stats 的具体动作实现仍然是页面层闭包回调
  - `connect()` 这个页面层入口还在，只是已经很薄；下一步应继续把 lifecycle action 实现也服务化下沉

补充进展（2026-06-27 本轮新增）：

- 页面层已经不再保留旧的 `connect()` 过程函数，改成更薄的：
  - `ensureSocketConnection()`
- 这一步的区别不是改名，而是语义收敛：
  - 页面层不再像以前那样拥有一整个 websocket 连接过程实现
  - 现在更像是在需要时“确保 transport 驱动的 socket 已建立”
- 这让当前页面层边界进一步贴近目标形态：
  - `initApp()` / reconnect 流程只需要触发一个很薄的连接入口
  - 具体的 socket 创建、handler 绑定、timer 调度已经更多落在 transport 侧
- 当前最真实的剩余差距继续收敛为：
  - `ensureSocketConnection()` 里的 lifecycle action 仍是页面层闭包实现
  - transport 还没有把这些 action 进一步抽成可复用的 socket runtime/service

补充进展（2026-06-27 本轮新增）：

- websocket lifecycle action 现在也不再直接内联在 `ensureSocketConnection()` 里：
  - 已收口成 `ensureSubagentTransport()` 内部的 `transportLifecycle`
  - 并通过 `subagent-transport` 暴露为 `transport.lifecycle`
- 这意味着当前连接入口已经进一步收敛为：
  - 页面层调用 `transport.connectSocket(url, transport.lifecycle)`
  - fallback 时才退回 `bindSocket(...)`
- 这一步的真实意义是：
  - 页面层不再同时持有“连接入口 + 生命周期闭包 + handler 路由”
  - transport 开始拥有更完整的 socket runtime 配置对象
- 当前最主要的剩余差距进一步收敛为：
  - `transportLifecycle` 仍定义在页面层初始化闭包内，而不是 transport 模块内进一步服务化
  - fallback 路径和少量 UI 依赖仍存在，尚未完全达到页面层只剩纯 wiring 的终态

补充进展（2026-06-27 本轮新增）：

- transport lifecycle 现在不再由页面层直接构造最终对象：
  - 页面层只提供 `lifecycleActions`
  - `subagent-transport` 内部通过 `createLifecycle(actions)` 生成最终的 `transport.lifecycle`
- 这一步的边界变化很关键：
  - 页面层不再直接持有“socket lifecycle 对象”这个结构
  - transport 自己开始拥有 lifecycle object 的构造职责
- 当前结构已经更像真正的 transport/runtime 组合：
  - 页面层提供依赖和动作
  - transport 负责生成 lifecycle、绑定 socket、管理 timer、路由消息
- 当前剩余差距继续收敛为：
  - `lifecycleActions` 仍然是页面层闭包，尚未拆成更独立的 service/action provider
  - fallback 路径和少量 UI 依赖仍然存在，页面层还没有完全退化为纯 declarative wiring

补充进展（2026-06-27 本轮新增）：

- Web 侧又新增了一层更贴近 `opencode stream.transport + runtime.lifecycle` 分工的页面级 chat transport：
  - `spiffs_data/web/subagent-chat-transport.js`
- 这一层不是重复造一个底层 transport，而是把原来仍残留在 `app.js` 的两类闭包继续收口：
  - chat shell websocket message handlers
    - `session_sync`
    - `upload_done / upload_error`
    - `stopped`
    - `tool / pet_response / reasoning / response`
  - socket lifecycle actions
    - `onOpen`
    - `onClose`
    - `onError`
    - `onMessage`
- 现在的边界变成：
  - `subagent-transport.js`
    - 底层 websocket/http 输入桥接
    - payload parsing / routing
    - snapshot fetch
    - socket bind/connect
    - timer resource helpers
  - `subagent-chat-transport.js`
    - chat shell 级 websocket handler / lifecycle 编排
    - 通过依赖注入消费页面层能力
  - `app.js`
    - 只负责装配依赖、触发 `ensureSocketConnection()`、保留最顶层 DOM wiring
- 这一步的价值不是“又拆一个文件”：
  - 之前虽然 `subagent-transport` 已经接管了 socket/timer/payload routing，但 `app.js` 里仍有一整块 `transportHandlers + transportLifecycleActions`
  - 那意味着 transport 行为的实际所有权依然在页面入口层
  - 现在这块被下沉到显式模块后，`app.js` 对 websocket 主链的角色进一步缩小成依赖提供者
- 这让当前 Web 分层更接近参考实现的真实精神：
  - 输入通道层有明确模块边界
  - lifecycle 行为不再和页面初始化代码完全混在一起
  - 后续如果继续演进到更彻底的 session-first/subagent-first stream runtime，不需要再先扒开 `app.js`
- 当前仍存在的差距也更清楚了：
  - `subagent-chat-transport` 仍通过注入依赖调用页面 helper，还不是完全自持的 session runtime service
  - chat shell 与 subagent stream 目前仍共用一个页面入口，而不是像 `opencode` 那样由更完整的 runtime/stream service 长期持有状态推进

补充进展（2026-06-27 本轮新增）：

- backend delegate scheduler 继续向集中式 runtime 收口：
  - worker 线程完成后不再自己直接触发 staged 下一跳 launch
  - launch ownership 保持在主循环 `delegate_launch_ready_background_subagents_for_runtime()`
- 同时，`tool_delegate_dispatch.c` 现在新增了统一后台并发预算：
  - 环境变量：`DELEGATE_BG_MAX_CONCURRENCY`
  - 默认上限：`DELEGATE_TASK_STORE_MAX`
  - 发车口在 coordinator scan 时会读取全局 `delegate_task_store_running_count()`，超过预算则保留 queued，不再继续起 worker
- 这一步的意义不是简单限流：
  - 之前系统是“一任务一 detached thread”，缺少统一 launch budget
  - staged 子任务虽然有 dependency gate，但没有全局并发 gate
  - 现在至少把“是否继续发车”重新统一到 scheduler 语义里，而不是任由 coordinator 在单次 scan 里无限起线程
- 和 `oh-my-openagent` / `opencode` 的差距仍然存在，但更明确：
  - 现在已有集中式 launch ownership 和全局 budget
  - 但底层仍不是 thread-pool / event-loop style 的完整 scheduler service
  - coordinator 之间的公平性还只是“按扫描顺序 + 当前全局 budget”
  - 还没有更丰富的 policy：优先级、backpressure、per-parent/per-team 配额

补充进展（2026-06-27 本轮新增）：

- `child_session` 的真实历史消息现在已经进入统一共享序列化层：
  - `kernel/tooling/delegate/delegate_session_json.c`
  - HTTP `/api/subagent_state`
  - WebSocket `subagent_session / coordinator_status / coordinator_done`
  都不再各自维护一份 `child_session` JSON 拼接逻辑
- 新协议点：
  - `child_session.history = [{role, content, reasoning?}]`
  - 数据直接复用 delegate child 的真实 `session_store` 持久化历史
  - 这样前端 detail 面板看到的 assistant/reasoning transcript，不再依赖散落在各处的 ad-hoc 提取
- 2026-06-27 本轮继续补齐了一处关键缺口：
  - 普通 delegated child run 完成后，也会显式把
    - child user prompt
    - child assistant final text
    - child reasoning
    持久化进对应 `session_store`
  - 这意味着 `child_session.history` 不再主要依赖：
    - shortcut 路径
    - 手工注入的测试会话
    - 最终 256 字摘要 commit
  - 而开始对真实 child turn 的 transcript 有稳定覆盖
  - 新增回归：
    - `delegate turn session persists full child transcript`
- 这一步的价值不只是“少重复代码”：
  - 它把 `child_session` 的协议重心继续往 `session replay source` 靠拢
  - 避免后面继续出现：
    - HTTP 有 `history`
    - WS 没有 `history`
    - 或者两边字段细节漂移
  这种 session-first 架构最忌讳的分叉

补充进展（2026-06-27 本轮新增）：

- `child_session` 的 history retention 窗口已经从极小的 8/12 条提升到更实用的 32 条
  - 后端：
    - `DELEGATE_SESSION_FRAME_LIMIT = 32`
    - `DELEGATE_SESSION_COMMIT_LIMIT = 32`
    - `DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT = 32`
  - 前端 reducer：
    - `DETAIL_TIMELINE_LIMIT = 32`
    - `DETAIL_COMMIT_LIMIT = 32`
    - `DETAIL_HISTORY_LIMIT = 32`
- 这一步的目的不是单纯“多存一点”，而是解决真实多步骤子会话里：
  - 早期 `subagent_step`
  - 中途 `subagent_request / subagent_blocked / subagent_unblocked`
  - 终态 `subagent_done`
  被 8 条窗口快速挤掉的问题
- 当前仓库已经新增针对这一点的回归：
  - `delegate task store retains richer child session history window`
  - `delegate child session json retains recent history window`
- 这让 `daima-agent` 在子会话细节保留能力上更接近 `opencode` 的 `SUBAGENT_COMMIT_LIMIT = 80` 思路，虽然仍然没有完全达到同等量级

补充进展（2026-06-27 本轮新增）：

- `child_session` 的 pending interactive request 不再只以 `pending_queue` 快照形式暴露
  - 后端现在会在 `delegate_task_store_set_pending_request()` 对应的 session 投影里追加结构化 session frame / commit
  - frame 类型为 `subagent_request`
  - commit 类型会区分 `question` / `permission`
- 这让 Web detail timeline 不必再仅靠 `pending_queue` 或 blocker 文本反推“子会话刚刚发起过问题/权限请求”
  - 更接近 `opencode` 的 `session-data` 模型：阻塞请求既存在于 queue，也存在于 session event history
- 当前这一步仍然只是 session-first 的一部分：
  - 还没有完整 transcript / tool-call 级 stream
  - 但 child session 已经从“只有结果摘要”进一步推进到了“结果 + blocker queue + blocker/request history”

补充进展（2026-06-27 本轮新增）：

- `child_session` 现在不再只有：
  - `tool`
  - `blocker`
  - `result`
  这种偏状态机/工具侧的 commit
- 后端新增了显式的 child-session message 投影：
  - `delegate_task_store_append_session_message(...)`
  - projection 会产出：
    - frame type: `subagent_message`
    - commit kind: `assistant` / `reasoning`
- 同步 child turn 执行完成后，`run_sync_single_subagent(...)` 会把：
  - child assistant final text 摘要
  - child reasoning text 摘要
  写入 `child_session`
- 这一步的意义不是“多打一条日志”，而是让 child session 开始拥有比 `tool/blocker/done` 更接近 transcript 的语义层
  - 以前 detail 里看到的大多是：
    - 开始
    - 工具调用
    - 阻塞
    - 完成
  - 现在开始能看到：
    - 子代理自己产出的 assistant 结论摘要
    - 子代理 reasoning 摘要
- 它仍然不是 `opencode` 那种完整 message-by-message scrollback
  - 还没有 part/delta 级 replay
  - 也没有完整 message list bootstrap
  - 但已经不再是“只看工具和状态机，不看子会话语言内容”
  - 这是真正向 `session-data / session-replay` 方向挪动，而不是继续堆特例事件

补充说明：

- 当前 Web 已经具备一版 `tabs + details + blockers + frames` 的基础模型：
  - `spiffs_data/web/app.js`
  - `spiffs_data/web/subagent-state.js`
  - `subagentUiState = { coordinators, tabs, details, selectedTabKey }`
- 当前这一层的职责也已进一步收口：
  - `subagent-state.js` 负责 coordinator/session/detail 的归一化和 reducer
  - coordinator 排序、默认选中 subagent、可见 tabs 这类 session-first selector 也已收进 `subagent-state.js`
  - `app.js` 负责 websocket 事件适配、interactive UI、DOM 投影
  - `app.js` 不再维护第二份 coordinator store，这一点是继续向 `opencode` 的 session-first 分层靠拢的基础
  - 2026-06-27 这轮又继续收口了一步：
    - `eventLog`
    - `interactiveBlockers`
    - snapshot hydrate
    - `blockerForDetail`
    都已经下沉到 `subagent-state.js`
  - 现在 `app.js` 已不再自己维护第二份 `subagentEventLog / interactiveBlockersByKey`，HTTP `/api/subagent_state` 恢复也直接走 reducer hydrate
  - 这让“实时 websocket 增量事件”和“HTTP 重连快照恢复”终于共享同一份 session/detail/blocker 状态模型，不再由页面层自己拼装两套来源
- 2026-06-27 这轮修正后，WebSocket 的
  - `subagent_start`
  - `subagent_progress`
  - `subagent_done`
  - `subagent_blocked`
  - `subagent_unblocked`
  都已接入前端 reducer，不再丢 blocker 事件。
- 目前仍弱于 `opencode` 的核心点，已经收敛为：
- 还没有真正的子会话 scrollback/commit 流，目前是 `child_session snapshot(frames + commits + pending_queue + assistant/reasoning summary commits) + subagent event frames`
- 当前虽然把 child-session retention 提升到了 32 条，并且统一了 frame/commit/history 三个窗口，但仍明显弱于 `opencode` 的 80 条级别窗口，也没有真正的 transcript/message stream
- runtime/web UI 侧已经补出通用 `interactive_request / interactive_reply` 基础设施，前端也能消费 `sudo_password` 与 `question_text`
- `turn_interview` 已经改成在 websocket implement 场景下优先发 `question_text`，并把用户回答拼回当前消息继续执行
- 这条链路之前缺的 websocket interview 阻塞/恢复稳定回归，现在已经补上
- reducer 虽已拆到 `subagent-state.js`，但还没有继续细分为事件适配层 / 视图层
- 2026-06-27 本轮继续收口了一处 Web 侧双真相：
  - interactive modal 当前活动请求不再由 `app.js` 额外维护 `activeInteractiveRequest` 镜像
  - 现在这份状态只保留在：
    - `spiffs_data/web/subagent-interactive-controller.js`
  - `app.js` 改成通过 controller API 读取当前 request，再生成：
    - `interactive_reply`
    - `subagent_unblocked`
    - blocker clear action
  - 这一步虽然不大，但它直接减少了：
    - reducer / controller / app-page
    三处对同一 interactive request 的并行持有
  - 对标 `opencode`，这更接近“session reducer + footer/controller 投影”模式，而不是页面层再保存一份状态副本
- 2026-06-27 本轮继续收口了 coordinator panel 的页面级镜像状态：
  - `app.js` 不再长期持有
    - `coordinatorVisible`
    - `coordinatorDismissed`
    两个页面级副本
  - 现在 dock/render/hide 都改为直接读取：
    - `spiffs_data/web/subagent-coordinator-controller.js`
      - `state()`
      - `setRendered()`
      - `hide()`
      - `close()`
  - 这意味着：
    - detail dock 状态
    - coordinator render open/close 语义
    - manual close 后的 dismiss 语义
    开始共享同一个 controller truth，而不是 `app.js` 再维护一对并行布尔值
  - 这一层虽然仍未达到 `opencode` 那种更彻底的 footer/view reducer 结构，但已经把 coordinator 面板从“页面脚本手搓可见状态”往 controller-owned projection 推近了一步
- 2026-06-27 本轮继续把 coordinator 的 dock orchestration 从页面层收进 controller：
  - `subagent-coordinator-controller.js` 现在在这些状态迁移上会主动触发 `syncDockState(...)`
    - `reset()`
    - `markActive()`
    - `render(...)`
    - `hide()`
    - `close()`
  - `app.js` 不再在 render/hide/close 之后手工补 `syncSubagentDetailDockState()`
  - 这样 coordinator panel 的“显示状态变化”与 detail dock 的“布局状态变化”开始共享同一个 controller 生命周期
  - 这和 `opencode` 里 footer view 由 reducer/controller 驱动，而不是页面层在每个事件后到处 patch 的方向更一致
- 2026-06-27 本轮继续把 coordinator panel 的 view-model 组装从 `app.js` 下沉到 selector：
  - `subagent-state-selectors.js` 新增：
    - `coordinatorSummaryText(state)`
    - `coordinatorPanelViewModel(state, panelState)`
  - `app.js` 不再自己长期拼：
    - `orderedStates`
    - `detailStates`
    - `summary`
    - coordinator summary 文案
  - 现在 coordinator panel 渲染输入开始更明确地属于 selector/view-model 层，而不是页面脚本本身
  - 这一步对齐的是 `opencode` 里：
    - reducer/selector 负责语义态和 view model
    - footer/view 负责投影
    - 页面入口只做 transport/orchestration
- 2026-06-27 本轮继续把 coordinator agent 的纯 DOM 渲染从 `app.js` 下沉到 view 模块：
  - `subagent-coordinator-view.js` 新增 `renderCoordinatorAgent(...)`
  - `app.js` 不再直接持有那大段 `.coordinator-agent` DOM 结构拼装代码
  - 页面层现在只负责注入依赖：
    - `formatElapsed`
    - `resolveAgentRole`
    - `detailKeyForAgent`
    - `subagentEventsForAgent`
    - `onSelectDetail`
  - 这让 coordinator UI 的边界继续清晰：
    - selector 产出 view-model
    - view 模块负责 DOM 投影
    - `app.js` 负责 wiring
- 2026-06-27 本轮继续把一组 coordinator 展示辅助语义从页面层收进 selector facade：
  - 现在这些函数不再由 `app.js` 本地定义：
    - `resolveAgentRole(...)`
    - `formatElapsed(...)`
    - `subagentFocusLabel(...)`
    - `coordinatorAgentHint(...)`
  - 它们统一迁到：
    - `subagent-state-selectors.js`
    - 并通过 `subagent-state.js` facade 导出
  - 这一步虽然看起来细，但它继续压缩了页面层的职责边界：
    - 页面入口不再自带一套 coordinator 展示语义
    - 而是消费 selector/view 层已经定义好的格式化规则
- 2026-06-27 本轮删除了 Web 页面层遗留的 coordinator output 镜像缓存：
  - `app.js` 不再维护 `agentOutputs`
  - `syncCoordinatorOutputs(...)` 也被移除
  - coordinator 卡片与 detail 面板现在都直接依赖 reducer / coordinator payload 已经提供的 `output`
  - 这一步的意义不是“少一个对象”，而是继续消掉页面层对 subagent 最终输出的第二份记忆
  - 这让 output 的真实来源进一步收敛到：
    - reducer detail state
    - 当前 coordinator payload / selector 派生结果
- 2026-06-27 本轮继续把 tab 选择切回 reducer action 入口：
  - `subagent-state-reducer.js` 新增 `select_tab` action
  - `app.js` 不再直接写 `subagentUiState.selectedTabKey = ...`
  - coordinator 卡片点击、detail tab 点击，现在都会先派发 `select_tab`
  - 这一步的核心价值是：
    - selected tab 不再是页面层对状态对象的裸写
    - 而是开始进入显式状态机入口
  - 这更接近 `opencode` 的 reducer 驱动模式，也为后续继续抽象更多 UI 选择/切换动作打基础
- 2026-06-27 本轮继续把 interactive dismiss 的 action 组装从页面层下沉到 adapter：
  - `subagent-event-adapter.js` 新增：
    - `makeInteractiveDismissActions(request, helpers)`
  - `app.js` 在关闭 interactive prompt 时，不再自己分别拼：
    - `subagent_unblocked` event action
    - `interactive_blocker_clear` action
  - 而是统一消费 adapter 产出的 dismiss actions
  - 这一步继续减少了页面层直接理解 reducer/internal action 协议的范围
  - 方向上更接近 `opencode` 的 transport/reducer adapter 先吸收协议，再由壳层触发
- 2026-06-27 本轮继续删掉了一层 interactive prompt 的页面级 passthrough：
  - `app.js` 里的 `openInteractivePrompt(...)` 已移除
  - interactive request 现在直接走：
    - adapter 产出 `controllerState`
    - `setInteractiveControllerState(...)`
    - `interactiveController.apply(...)`
  - 这一步虽然不大，但它继续把 interactive prompt 的入口收敛到 controller state apply，而不是页面层再包一层 prompt-open helper
- 2026-06-27 本轮继续把 interactive reply 的输入读取收进 controller：
  - `subagent-interactive-controller.js` 新增 `currentValue()`
  - `app.js` 在提交 interactive reply 时，不再直接读取 `interactiveInput.value`
  - 而是统一通过 controller 读取当前输入值
  - 这一步继续减少了页面层对 interactive DOM 细节的直接依赖
- 2026-06-27 本轮继续把 interactive 清理路径统一到 controller：
  - `app.js` 在这些路径上不再自己 fallback 执行：
    - `interactiveModal.classList.remove('show')`
    - `interactiveInput.value = ''`
  - 现在 close prompt / replace snapshot 都统一走：
    - `interactiveController.clear()`
  - 这进一步压缩了页面层对 interactive DOM 的直操作面
- 2026-06-27 本轮继续把 interactive reply payload 组装收进 controller：
  - `subagent-interactive-controller.js` 新增：
    - `buildReplyPayload(makePayload, chatId, cancelled)`
  - `app.js` 在提交 interactive reply 时，不再自己组合：
    - active request
    - input value
    - chatId
    - cancelled flag
  - 现在统一由 controller 产出 reply payload
  - 这让 interactive 提交流程继续从“页面脚本拼协议”往“controller 产出提交语义”推进
- 2026-06-27 本轮继续补齐了运行时级 child-session 投影：
  - `tool_runtime_execute_call(...)` 现在会在识别到当前 `chat_id` 属于 delegate child session 时，自动向 `delegate_task_store` 追加 `subagent_step`
  - 这意味着普通 delegated LLM child run 的真实工具调用，不再只有：
    - `preflight_tool`
    - `local_overview_shortcut`
    - `dependency_merge_shortcut`
    这些特例路径可见
  - 现在像 `get_current_time`、`terminal`、`files` 等真实运行时工具调用，也会进入 `child_session.frames/commits`
  - 这一层更接近 `opencode` 的 `session-data` 思路：真实 child session activity 由中心运行时统一投影，而不是各条 delegate 分支各自打补丁
  - 当前仍然不是完整 transcript stream，但“普通工具调用完全不可见”的核心差距已经被消除
- 2026-06-27 本轮继续把前端认知中心从 `coordinator-first` 往 `session-first` 推了一步：
  - `subagent-state.js` 新增了面向 detail/session 的 selector：
    - `orderedSubagentDetails(state)`
    - `subagentSummary(state)`
  - `app.js` 的 coordinator 面板标题和摘要不再只按“几批 coordinator 在跑”来表达
    - 现在优先按真实 subagent/session detail 聚合：
      - 运行中数量
      - 阻塞数量
      - 完成数量
      - 失败数量
  - 这一步虽然还没有把 UI 整体改成 `opencode footer/subagent pane` 结构，但已经把“页面主语”从 coordinator batch 往 child session 本身收口
  - 也就是说：
    - coordinator 继续承担后台批次与 parent wake 生命周期
    - session/detail 开始承担用户实际看到的一级对象与状态摘要
  - 这是继续靠近 `opencode subagent-data` 的必要中间层，而不是单纯调文案

### 2.4 当前最值得继续补的点

如果继续按 `oh-my-openagent + opencode` 的方向推进，而不是做表面 patch，当前最值得补的不是“再加一个事件类型”，而是下面两项：

1. 把 `child_session` 从“结构化快照”继续推进到“更完整的 session stream”
   - 当前已经覆盖：
     - `preflight_tool`
     - `local_overview_shortcut`
     - `dependency_merge_shortcut`
     - `pending interactive request`
     - 普通 delegated child 的真实 runtime tool-call
     - delegated child 的 assistant / reasoning summary commits
   - 但现在仍然主要是 `frames + commits + pending_queue` 快照窗口，不是 `opencode` 那种更长窗口、可持续滚动的 session stream
   - 剩余价值点已经从“能不能看到普通工具调用”转成“能不能看到更完整、更多轮次、更像 transcript 的 child session 演进”

2. 继续把 Web 层从“coordinator 面板驱动”推进到“session-first 驱动”
   - 当前 reducer 已统一了 websocket 增量和 HTTP bootstrap hydrate
   - 当前标题/摘要 selector 也开始按 subagent/session detail 聚合，而不是只按 coordinator 统计
   - 但 UI 选择、footer tab、detail view 仍主要是“挂在 coordinator 面板下的 detail”
   - 离 `opencode` 那种“subagent session 作为一级对象”的模型，还差最后一层：把 session list/detail/footer 真正独立成一级交互结构

补充进展（2026-06-27 本轮）：

- 已新增真实 websocket 回归脚本：
  - `scripts/dev/check-websocket-interview-recovery.sh`
  - `scripts/dev/probe-subagent-websocket.py`
- 当前这条链路已经有真实运行证据：
  - 父级先收到 `interactive_request(question_text)`
  - `/api/subagent_state` 在 coordinator 创建前就能返回顶层 `pending_request`
  - probe 自动发送 `interactive_reply`
  - 同一 turn 继续执行并启动多 subagent
  - 出现 `coordinator_status / coordinator_output / coordinator_done`
  - coordinator snapshot 中带 `child_session.frames`
  - 最终父会话收到本地 merge 后的汇总答复
- 这意味着“websocket implement 场景下的 interview -> HTTP bootstrap restore -> 恢复 -> 多 subagent -> parent final response”已从半真实链路进入稳定回归链路

### 2.3 仍弱于 `oh-my-openagent` 的点

`oh-my-openagent` 的 parent wake 更强在两个方面：

- 更完整的 prompt async gate / reserve / dedupe
- 对 session idle / output validity / todo unfinished 的 completion 判定更严谨

当前 `daima-agent` 已经有：

- wake retry
- terminal cleanup
- parent response gating

但还没有：

- 更强的 parent wake 去重语义
- 与 session idle / incomplete work 的更细 completion 判定
- 基于历史/状态的 prompt dispatch 判重

补充进展（2026-06-27 本轮）：

- `delegate_parent_wake.c` 已补上 failure/reply-required retained wake 语义
  - 新增自测：
    - `delegate parent wake retains failed resume after parent activity`
  - 当前普通 retained `done` resume 仍会在“父会话已继续前进”时被正确丢弃
  - 但 `failed` 终态不会再被同一分支吞掉，而会像 `oh-my-openagent` 的 failure wake 一样继续等待安全恢复
- 这意味着 parent wake 语义虽然仍弱于 `oh-my-openagent` 的完整 async gate/history suppression，但“failure wake 被误消费”这个高优先级 correctness 缺口已经补掉

---

## 3. 当前项目的核心优势与短板

### 3.1 优势

当前项目的优势是后端链路已经足够清晰：

- `delegate_task` 明确是唯一入口
- `delegate_task_store` 明确是状态中心
- `delegate_parent_wake` 明确是协调器推送与收尾中心
- `turn_entry` 明确负责父会话本地 merge

这套边界比“到处写 if / 关键词兜底”稳得多。

### 3.2 真实短板

现在最大的短板已经不是后端不会跑。真实运行时已经验证：

- 可以同时安排多个 subagent
- websocket 可以看到 `subagent_start/progress/done`
- coordinator 事件里已经带有每个 agent 的 `child_session`
- `coordinator_done` 后父会话会收到最终汇总回复

剩下的问题主要在前端体验深度和验证覆盖面：

- `child_session` 目前仍是压缩快照，不是完整 session transcript
- 但这个快照协议已经不再只有 summary，而是具备更接近 session 视角的
  - `frames`
  - `commits`
  - `pending_queue`
- Web 还没有做到 `opencode` 那种 session-first 浏览体验
- Web 还没有做到 `opencode` 那种彻底的 session-first 浏览体验，但有一个关键边界已经补掉：
  - 手动关闭 coordinator 面板时，不再直接清空 `subagentUiState`
  - 关闭现在只是隐藏当前视图，不会销毁已加载的 `detail / tabs / history / blockers`
  - 后续新的 `coordinator_status / coordinator_output` 到来时，面板会自动重新打开
  - 这让 subagent session/detail 不再只是 coordinator toast 的附属物，而开始具备独立生命周期
- interview / permission / question 的端到端回归还不够强
- 其中 `question_text` 的 websocket 端到端恢复回归已经补上
- parent 顶层 pending interview 的 HTTP bootstrap restore 也已经补上
- child session 的 pending queue 也已支持结构化 restore
- 仍可继续加强的，是带真实子任务 blocker 的 permission/question 混合阻塞场景

---

## 4. 2026-06-27 当前已验证的新进展

这轮之后，和 `oh-my-openagent` / `opencode` 对齐上，已经确认落地并验过的点有：

- staged 调度链路已经真实跑通，不再停留在 store 状态正确
  - 上游子任务完成后，下游 `depends_on` 子任务会自动启动
  - 下游 `oracle` 子任务在纯汇总场景下可走本地 `dependency_merge_shortcut`
  - `coordinator_done` 后父会话会收到最终 websocket 汇总回复
- runtime 调度器已经补上 coordinator 变更重扫
  - `kernel/loop.c` 会触发 runtime ready-task rescan
  - 子任务完成路径也会直接触发 rescan
  - queued 子任务启动使用原子 claim，避免并发 completion 下重复拉起
- pending interactive request 的 session 投影已打通
  - `child_session.pending_queue` 会随 `set_pending_request()` 刷新
  - 清除 blocked 状态时也会同步清空纯 pending-request 残留
- 本地 dependency merge 结果已从“给子代理的操作提示”收敛为“给父代理/用户的汇总结论”
  - 不再把 `Resolved repo root` 这类内部 prompt scaffolding 直接泄漏到最终汇总

- 构建系统已补上头文件依赖追踪
  - `scripts/Makefile.build` 现在会为每个 `.o` 生成并包含对应 `.d`
  - `Makefile clean/kbuild-clean` 也会一并清理 `.d`
  - 这修掉了一个会直接污染多 subagent 验证结果的基础缺陷：
    - 之前 `delegate_task_store.h` 这类共享协议头文件变更后，相关对象可能不会自动重编
    - 会出现“编译通过，但 runtime/self-test 因旧 ABI 混编而异常”的假失败
  - 多 subagent 场景尤其依赖这个修正，因为 `coordinator / child_session / pending_request` 都强依赖共享结构体布局一致
- `delegate` 终态父回复的 `source=delegate` 已透传到最终 websocket 出站消息
  - 修复前：最终父回复发出后，`channel_runtime_dispatch_outbound()` 会把它当普通父回复，再次触发 `parent_response_sent`
  - 修复后：终态 wake 生命周期在真实 runtime 中只出现一次
- `delegate_parent_wake` 的主循环刷新风暴已经消失
  - `delegate_task_store_drain_changed_coordinators()` / `poll_updates()` 先看 `changed` 再 refresh
  - 真实 runtime 针对 websocket mixed blocker probe，`delegate_store refresh entry` 次数已经降到 `0`
- Web 完成态 coordinator / subagent detail 不再 15 秒后自动删除
  - 旧行为更像临时 toast，不适合多 subagent 的 session-first 浏览
  - 当前行为更接近 `opencode` footer/subagent 面板：完成后结果继续可见，直到用户主动关闭

### 4.1 最新硬验证证据

构建与自测：

- `make -C /home/wangergou/code/github/daima-agent -j4`
- 全量清理后重编：
  - `find ... -name '*.o' -delete && rm -f build-kbuild/agent && make -C ... -j4`
- `./build-kbuild/agent --self-test`
- 2026-06-27 最新结果：退出码 `0`，`127/127 passed`

构建依赖验证：

- 已实证生成 depfile：
  - `kernel/tooling/delegate/delegate_task_store.d`
- depfile 中已正确记录：
  - `delegate_task_store_internal.h`
  - `delegate_task_store.h`
- 这意味着后续再修改 delegate store 协议头文件，相关对象会自动重编，不再需要人工全量清理才能得到可信结果

前端 UI 回归：

- `node scripts/dev/check-subagent-web-ui.js`
- 结果：通过，且新增覆盖

---

## 5. 目标架构蓝图

如果最终目标是“像 `oh-my-openagent + opencode` 那样，稳定、可恢复、可观察地支持多个 subagent”，那 `daima-agent` 后续不应该继续围绕零散事件加分支，而应该收敛到下面这套分层。

### 5.1 目标分层

#### A. Delegate Runtime Layer

职责：

- 解析 `delegate_task`
- 生成单任务或 batch/coordinator
- 管理 queued/running/done/failed/blocked
- 触发 downstream staged launch

当前对应：

- `drivers/tool/tool_delegate.c`
- `kernel/tooling/delegate/delegate_task_store.c`
- `kernel/tooling/delegate/delegate_task_runtime.c`

最终要求：

- `tool_delegate.c` 只保留“入口编排 + 请求解析 + 子任务启动”
- 任务状态推进、projection、resume 判定继续下沉到 `kernel/tooling/delegate/`
- repo-overview / bounded explore / dependency-merge 这类策略逐步从超大入口文件里拆到独立模块

#### B. Child Session Projection Layer

职责：

- 把 child turn 的真实运行过程投影成统一 session 视图
- 输出：
  - `history`
  - `frames`
  - `commits`
  - `pending_queue`
  - `session_summary`

当前对应：

- `kernel/tooling/delegate/delegate_task_projection.c`
- `kernel/tooling/delegate/delegate_session_json.c`
- `drivers/tool/tool_runtime.c`

最终要求：

- child session 以“真实会话 replay 源”为中心，而不是临时 UI 结构
- 不同来源统一投影到一套 session schema：
  - child assistant/user transcript
  - runtime tool call
  - blocker / request / resume
  - done / fail result
- HTTP snapshot 和 websocket 增量严格共用同一 schema

#### C. Parent Wake Orchestrator Layer

职责：

- 统一消费 changed coordinators
- 决定 websocket/status/done 推送时机
- 决定 parent resume 何时注入
- 处理 retry / dedupe / unsafe-parent deferral

当前对应：

- `kernel/tooling/delegate/delegate_parent_wake.c`
- `kernel/loop.c`
- `kernel/channel/channel_runtime.c`

最终要求：

- 继续向 `oh-my-openagent` 的 `flush runner + pending queue + session history gate` 靠拢
- “parent 是否安全”判定不只看最近 activity，还要引入更强的 parent history inspection
- retained reply-required wake、failure wake、admit-only wake 的语义彻底收口到这一层

#### D. Session-First Web State Layer

职责：

- 以 child session/detail 为一级对象，而不是 coordinator 卡片
- 统一接收：
  - websocket 增量事件
  - HTTP bootstrap snapshot
  - interactive blocker restore
- 提供 selectors：
  - tabs
  - selected detail
  - blocker queue
  - summary

当前对应：

- `spiffs_data/web/subagent-state.js`
- `spiffs_data/web/app.js`

最终要求：

- `subagent-state.js` 继续演进成真正的 `session-data + subagent-data` reducer
- `app.js` 只负责 transport / DOM binding
- coordinator 变成“后台批次视图”，不再是 detail/session 的父级概念

### 5.2 最终一级对象应该是什么

对齐 `opencode` 之后，用户真正看到和操作的一级对象应该是：

- `Parent session`
- `Subagent session`
- `Coordinator batch` 只是后台调度容器，不是用户主语

也就是说：

- coordinator 负责“这批任务怎么启动、谁依赖谁、父会话什么时候被唤醒”
- subagent session 负责“某个子代理到底做了什么、卡在哪、产出了什么、能否恢复”

这也是为什么当前已经做的 session-first 解耦是正确方向，而不是 UI 细节优化。

---

## 6. 当前剩余关键差距

基于当前代码而不是旧印象，离“完美支持多 subagent”还差的关键点已经收敛成 4 类。

### 6.1 `tool_delegate.c` 仍然过重

现状：

- 入口解析、repo-root 批量扩展、background launch、HTTP snapshot 输出、local shortcuts 都堆在一个巨型文件里

风险：

- 每次补新的多 subagent 策略，都容易把 runtime、projection、transport 再耦回入口层
- 很难像 `oh-my-openagent/opencode` 那样稳定演进多个独立子系统

建议目标：

- 至少拆成：
  - `delegate_request_parse.*`
  - `delegate_repo_overview_plan.*`
  - `delegate_batch_launch.*`
  - `delegate_http_snapshot.*`

### 6.2 child session 仍然是“压缩快照”，不是“持续会话流”

现状：

- 已经有 `history + frames + commits + pending_queue`
- 但 retention 还是短窗口
- 也没有像 `opencode` 那样以 event/session reducer 持续增量滚动

风险：

- 多步骤、长生命周期 subagent 仍会丢早期上下文
- Web detail 仍更像“最近摘要”而不是“真实子会话”

建议目标：

- 引入更完整的 child session event source
- 把当前 `delegate_session_json` 从“最终组包器”推进为“session replay snapshot builder”
- 后续把窗口从 32 提升到更接近 `opencode` 的 80 级别，并确认内存边界

### 6.3 parent wake 仍弱于参考实现

现状：

- 已经有：
  - pending queue
  - retry
  - retained failure wake
  - parent activity gating
- 但没有完整的 prompt-async reserve/dedupe/history suppression 语义

风险：

- 在父会话高活动、连续 tool wait、回复/无回复混合穿插时，仍可能出现过度注入、延迟过长或判定过粗

建议目标：

- 对照 `parent-wake-flush-runner.ts` / `parent-wake-session-history.ts`
- 给 `delegate_parent_wake.c` 增加更强的 history-based dispatch gate
- 把“父会话已消费 admitted wake”之类规则继续从 ad-hoc 条件收敛成统一判定函数

### 6.4 Web 还没完全达到 `opencode` 的 session-first 交互

现状：

- detail panel 已经独立
- reducer 已统一 websocket + HTTP hydrate
- blocker / commits / history 已能恢复

仍缺：

- 类似 footer/subagent tabs 的一级持续交互模型
- 更明确的 session list vs coordinator batch list 分层
- 更完整的 mixed blocker 可视化和恢复状态

建议目标：

- UI 主语从“Coordinator 面板 + detail”继续推进到“Subagent Sessions”
- coordinator 退为辅助视图

---

## 7. 最优实施顺序

如果按“最优选择”而不是“最小改动”推进，后续建议严格按这个顺序做。

### Phase 1. Delegate 内核继续模块化

目标：

- 把 `tool_delegate.c` 拆到可持续维护

完成标准：

- repo-root expansion / batch planning / queued launch / HTTP snapshot 至少拆成独立文件
- `tool_delegate.c` 明显瘦身，只保留入口编排和薄封装

原因：

- 不先拆入口文件，后面 session stream 和 wake gate 继续加进去，只会让主入口越来越不可控

### Phase 2. Child session 升级为更完整 replay 源

目标：

- 从“snapshot 可看”升级到“session 真可回放”

完成标准：

- child session 的 transcript / tool / blocker / result 都来自统一 replay 数据源
- HTTP bootstrap 与 websocket live 增量的 detail 内容不再存在语义分叉

原因：

- 这是对齐 `opencode session-data / subagent-data` 的核心，不做这步，多 subagent 只能算“可见”，不能算“完美支持”

### Phase 3. Parent wake 强化为 history-gated dispatcher

目标：

- 对齐 `oh-my-openagent` 的 parent wake correctness

完成标准：

- 对 reply-required / noReply / failure retained wake 有清晰、统一、经自测验证的判定
- 新增覆盖父会话活跃、silent tool、admitted wake consumed 等关键回归

原因：

- child session 做强后，如果 parent wake 仍然粗糙，最终用户仍会感知为“多 subagent 不稳定”

### Phase 4. Web 完成 session-first 交互闭环

目标：

- 让 Web 使用体验真正接近 `opencode`

完成标准：

- subagent session 成为一级对象
- coordinator batch 退居辅助视图
- detail/blocker/history/interactive restore 在刷新和重连后都稳定

原因：

- 到这一步，后端和前端的主语才真正一致，项目才能说“架构上完整支持多 subagent”

---

## 8. 当前结论

截至 2026-06-27，`daima-agent` 已经不再是“只会假装多 subagent”的状态。

现在可以明确成立的判断是：

- 后端已经具备真实 coordinator/store/wake/session projection 主链
- Web 已经具备 session-first 的雏形，而不是纯日志面板
- 当前最大问题不再是“有没有多 subagent”，而是：
  - delegate 入口过重
  - child session 还不够像真实持续会话
  - parent wake correctness 还没完全追平 `oh-my-openagent`
  - Web 交互模型还没完全追平 `opencode`

所以后续正确方向不是继续补临时关键词或事件分支，而是沿着这条主线推进：

1. 先把 delegate 入口拆干净
2. 再把 child session 升级成更完整 replay 源
3. 再把 parent wake 收口成更强的 history-gated dispatcher
4. 最后把 Web 完整收敛到 session-first 模型

只要按这个顺序做，项目就会越来越接近 `oh-my-openagent + opencode` 的成熟多 subagent 架构，而不是继续在局部 patch 上打转。
  - `coordinator_done` 后 detail 仍可见
  - completed coordinator card 仍可见

HTTP + WebSocket replay 回归：

- `bash scripts/dev/check-websocket-http-subagent-replay.sh`
- 最新验证结果确认：
  - shell 退出码 `0`
  - `saw_http_question_snapshot = true`
  - `saw_http_completion_snapshot = true`
  - `saw_parent_final_response = true`
  - `saw_child_session_frames = true`
- 这说明：
  - parent 顶层 `question_text` interview 可以先通过 HTTP 恢复
  - 用户回复后同一 turn 继续执行
  - 多 subagent 完成后 coordinator 与 child session snapshot 可通过 HTTP/WS 一致观察

结构化 pending queue 回归：

- `node scripts/dev/check-subagent-web-ui.js`
- `./build-kbuild/agent --self-test`
- 最新验证补充确认：
  - `child_session.pending_queue.permissions/questions` 现在可恢复为对象项
  - UI 详情面板能继续显示这些结构化项的 `prompt`
  - self-test 已验证父唤醒快照里包含 `request_type / request_id / prompt`

staged websocket 回归：

- `bash scripts/dev/check-websocket-staged-subagent.sh`
- 2026-06-27 最新验证结果确认：
  - shell 退出码 `0`
  - `saw_completion = true`
  - `saw_parent_final_response = true`
  - `saw_staged_progress = true`
  - `saw_staged_dependency_child = true`
  - `saw_child_session_frames = true`
- 这说明：
  - staged coordinator 的上游完成后，下游依赖任务确实启动了
  - 下游依赖子任务已能通过 websocket 被前端观察到
  - 即使下游走本地 dependency merge shortcut，也不会再因为过快完成而被误判为 staged 链路失败
  - `parent wake` 的 recent-activity defer 也已经在真实 runtime 中闭环：
    - 先发启动响应
    - 终态进入 defer
    - 约 2 秒后再统一恢复父会话
    - 最终父会话拿到本地 merge 汇总回复

最新 staged runtime 关键日志证据：

- `deferred parent resume coordinator=dc_1 ...`
- `enqueue parent resume coordinator=dc_1 ...`
- `parent resume enqueued coordinator=dc_1 ...`
- `Queue final response ... (1808 bytes)`

这说明：

- websocket 可见事件和父最终恢复已经真正解耦
- 终态 `parent resume` 不再被 worker 线程并发抢跑
- staged coordinator 已具备接近 `oh-my-openagent` 的“先可见、后统一恢复父会话”语义

并发多父会话 websocket 回归：

- `bash scripts/dev/check-websocket-concurrent-subagents.sh`
- 2026-06-27 最新验证结果确认：
  - shell 退出码 `0`
  - 两个不同 `chat_id` 可并发各自启动 coordinator
  - 两侧都观察到：
    - `saw_completion = true`
    - `saw_parent_final_response = true`
    - `saw_child_session_frames = true`
  - 本次实际观测到的 `coordinator_id`：
    - chat A: `dc_1`
    - chat B: `dc_8`
  - 两组 `coordinator_id` 无重叠、无串话

这补上了之前还缺的一块关键证据：

- 不是只有“单个父会话里可以并行 4 个 subagent”
- 而是“两个父会话可以同时各自跑 coordinator，并且各自完成 parent wake 与最终汇总”

这点很重要，因为它更接近 `oh-my-openagent + opencode` 在真实产品里要承受的调度模型：

- 多 session 并发
- 各自独立 coordinator 生命周期
- 各自独立 parent wake / final reply 收尾

断连重连 websocket + HTTP 恢复回归：

- `bash scripts/dev/check-websocket-reconnect-subagent.sh`
- 2026-06-27 最新验证结果确认：
  - shell 退出码 `0`
  - 第一段 websocket 在父级 `question_text` interview 后主动断开
  - `/api/subagent_state` 仍保留 root-level `pending_request`
  - 第二段 websocket 仅发送 `interactive_reply`，无需重新发用户问题
  - 继续执行后观察到：
    - `saw_coordinator_status = true`
    - `saw_coordinator_output = true`
    - `saw_child_session_frames = true`
    - `saw_completion = true`
    - `saw_parent_final_response = true`
  - `/api/session_history` 计数从 `0` 增长到 `4`

这条回归证明了当前链路已经跨过一个关键门槛：

- 父级 interview 不再要求 websocket 一直不断线
- root pending interview 可以通过 HTTP 恢复
- 重连后仅靠 `interactive_reply(chat_id=原会话)` 就能重新绑定 websocket 会话并继续执行
- 最终 coordinator 完成态和父会话最终回复仍能落回重连后的 websocket 与 history

websocket 回归脚本稳定性修正：

- 之前 `scripts/dev/check-websocket-*.sh` 普遍依赖：
  - `run.sh --background`
  - `sleep 2`
  - 直接开始 websocket probe
- 这在单独运行时经常“看起来能过”，但本质上有两个脆弱点：
  - `run.sh` 会先杀旧 agent 再起新 agent，不适合把多条脚本并行跑在同一个端口环境里
  - 固定 `sleep 2` 不能证明 `ws_server` 和 `/health` 已真正 ready
- 2026-06-27 本轮已补：
  - `scripts/dev/ensure-agent-ready.sh`
  - 各条 websocket 回归脚本在 probe 前统一等待：
    - `GET /health = 200`
    - runtime log 出现 startup marker
- 这让验证矩阵终于能把“脚本启动竞态”与“runtime 真 bug”区分开：
  - 本轮 `check-websocket-http-subagent-replay.sh`
  - `check-websocket-reconnect-subagent.sh`
  在串行运行下都已恢复稳定通过

真实 websocket mixed blocker probe：

- `scripts/dev/probe-subagent-websocket.py`
- 当前已验证 chat:
  - `web_probe_mixed_blockers_final_1782531584`

最新 runtime log 计数（对应上面这次真实 probe）：

- `wake_pending = 1`
- `wake_dispatched = 1`
- `wake_completed = 1`
- `delegate_store refresh entry = 0`
- `Queue final response to websocket:<chat_id> = 2`
  - 1 次启动后台任务确认
  - 1 次最终父回复

这证明两件关键事实：

- 终态 duplicate wake 已经被真实 runtime 消掉
- 高频 refresh / 日志风暴也已经被真实 runtime 消掉

---

## 5. 现在还没到“完美支持多 subagent”的剩余差距

虽然当前链路已经比之前稳很多，但如果目标是接近 `oh-my-openagent + opencode` 组合的“完美支持”，还剩这些真实差距：

- `child_session` 仍是快照，不是完整持续 transcript/stream
  - 已有 `frames + commits + pending_queue`
  - 但仍不是 `opencode` 那种完整 session replay / resize replay / scrollback 恢复
- Web 仍是 coordinator-first 上层视图
  - 虽然 reducer 已拆好、detail/tabs 也已经具备
  - 但还没有真正把“子 session 是一等对象”的交互模型完全做透
- parent wake 仍缺 `oh-my-openagent` 那种更强的 async gate / reserve / history-based suppression 语义
  - 当前已具备 retry / recent-activity deferral / visible_revision dedupe
  - 但还没有更丰富的“历史已消费 / prompt gate 保留 / noReply admit”语义层
- 父回复本地 merge 的文本质量仍弱于 `oh-my-openagent`
  - 虽然现在已经不会再混入明显的内部 prompt scaffolding
  - 但输出仍偏“调试摘要 + 原始子任务摘录”，缺少更稳定的结构化最终答复模板
- permission / question blocker 目前已可展示、可回放、可做 websocket 回归
  - 但在 richer multi-parent / reconnect / long-lived session 场景下，还没做到像 `opencode` 那样稳定的 session replay 一致性
- parent wake 的 dedupe / idle / validity 语义仍弱于 `oh-my-openagent`

---

## 4. 建议的下一阶段改造顺序

### 阶段 1：补真实交互验证

先把“已经设计出来但还没跑透”的真实交互场景补成稳定验证：

- websocket interview 阻塞 -> 用户回复 -> 同一 turn 恢复
- permission/question blocker 在父子会话中的显示与解除
- parent wake 在 client 断开 / 重连下的行为
- dedupe 同 revision / repeated wake / repeated done

因为这条链路决定的是“多 subagent 是否真的可长期用”，不只是“本地自测能过”。

### 阶段 2：把 `child_session snapshot` 继续往 session stream 演进

参考：

- `github/opencode/packages/opencode/src/cli/cmd/run/subagent-data.ts`
- `github/opencode/packages/opencode/src/cli/cmd/run/session-data.ts`

继续把当前模型从“事件卡片 + 快照”推进到“子会话流”：

- richer commits
- tool/permission/question frames
- session summary 和 pending queue 的增量更新
- 更清晰的 child session schema

目标不是抄 TS 代码，而是把协议和 reducer 的层次补齐。

### 阶段 3：继续补 Web 的 session-first 视图

在 Web 里增加：

- 点击 agent 卡片查看详情
- 展示该 subagent 更完整的 commits / blockers / queue
- 让父会话 blocker 与子会话 blocker 的切换关系更清晰

这样才能更接近 `opencode + oh-my-openagent` 的可观察性体验。

当前进展：

- 详情视图、tab 切换、blocker 展示已经落地。
- `subagent` reducer 已从 `app.js` 拆出到 `spiffs_data/web/subagent-state.js`，后续再补 blocker/question 模型时不需要继续把状态机堆回主文件。
- 2026-06-27 的真实 websocket 探针已经验证：
  - 可以并行启动 4 个 subagent
  - 可以收到 `coordinator_status / coordinator_output / coordinator_done`
  - 每个 agent 都能在 coordinator 事件中携带 `child_session.summary / commits / pending_queue`
  - `coordinator_done` 后父会话能收到最终汇总回复
- 已新增开发验证脚本：
  - `scripts/dev/check-subagent-web-ui.js`
- 这个脚本会在最小 DOM 环境下加载
  - `spiffs_data/web/index.html`
  - `spiffs_data/web/app.js`
  然后注入一组 coordinator/subagent 事件，验证：
  - 多 tab 是否渲染
  - 选中态是否同步到 coordinator 卡片
  - blocker 是否在 detail panel 中可见
- 运行方式示例：
  - `npm install jsdom`
  - `node scripts/dev/check-subagent-web-ui.js`

因此阶段 3 现在不再是“完全未做”，而是“基础可见性已具备，离 opencode 的完整子会话流还差一层”。

### 阶段 4：补 parent-wake completion 语义

继续参考 `oh-my-openagent`：

- 更严格的 completion 判定
- 更强的 wake 去重与重试语义
- 避免重复注入父会话

---

## 5. 当前结论

结论很明确：

- `daima-agent` 现在已经具备“真正可运行的多 subagent 后端链路”
- 但还没有达到 `opencode` 那种“前端状态模型完整、子会话可细看”的完成度
- 下一阶段重点不该再回到关键词补丁，而应该继续补
- reducer
- detail 视图
- parent-wake 自测与收尾稳定性

也就是说，当前项目已经跨过“是否支持多 subagent”的门槛，接下来要攻的是“是否像参考实现一样，把多 subagent 做成稳定、可见、可操作的产品能力”。

---

## 6. 当前代码架构图谱

如果只看当前仓库，建议把多 subagent 能力按下面 6 层理解：

### 6.1 回合入口层

- `kernel/loop.c`
- `kernel/turn/turn_entry.c`
- `kernel/turn/turn_run.c`
- `kernel/turn/turn_exec.c`

职责：

- 接住用户输入
- 组织主回合 prompt / tool loop
- 在 `delegate_task` 完成后，把最终结果返还给父会话

判断标准：

- 这里只关心“本轮主回合如何继续”
- 不应该再持有 coordinator 级状态机

### 6.2 委托入口层

- `drivers/tool/tool_delegate.c`

职责：

- 作为唯一的多 subagent 工具入口
- 解析同步/后台/批量委托协议
- 规范化 child prompt、preflight tool、target_path、depends_on
- 启动后台 worker 或同步单次子代理

判断标准：

- 这里应该负责“怎么发起委托”
- 不应该吞并 parent wake / web reducer / parent merge 的职责

### 6.3 状态中心层

- `kernel/tooling/delegate/delegate_task_store.c`
- `kernel/tooling/delegate/delegate_task_projection.c`
- `kernel/tooling/delegate/delegate_task_query.c`
- `kernel/tooling/delegate/delegate_task_runtime.c`

职责：

- 保存 `task record` 和 `coordinator record`
- 维护 `queued/running/done/failed/blocked`
- 维护 `child_session.summary / frames / commits / pending_queue`
- 维护 `wake_state / visible_revision / completion_notified / parent_resume_enqueued`

判断标准：

- 这一层是 session-first / coordinator-first 的事实来源
- Web 和 parent wake 都应优先消费这里的结构化状态

### 6.4 父唤醒与广播层

- `kernel/tooling/delegate/delegate_parent_wake.c`

职责：

- drain changed coordinators
- 处理 retry / activity deferral / visible revision sent
- 发出
  - `coordinator_status`
  - `coordinator_output`
  - `coordinator_done`
  - `subagent_*`
  - `subagent_session`
- 在终态时给父会话注入本地 resume message

判断标准：

- 这里应该像 `oh-my-openagent` 的 background-agent flush runner 一样，承担“如何安全地通知父级”
- 不应该再让 `tool_delegate.c` 或 WebSocket 通道驱动直接散落终态逻辑

### 6.5 交互阻塞层

- `kernel/interactive.c`
- `kernel/interview.c`
- `kernel/turn/turn_interview.c`
- `kernel/turn/turn_context.c`

职责：

- 处理 `question_text`
- 处理 `sudo_password`
- 处理父级 pending_request 与 child pending_request 的存储、恢复、继续执行

判断标准：

- 这里决定“多 subagent 在交互阻塞时是否还能恢复”
- 它和多 subagent 的耦合点，应该只通过结构化 pending request 协议连接

### 6.6 Web 展示层

- `spiffs_data/web/app.js`
- `spiffs_data/web/subagent-state.js`

职责：

- websocket 事件适配
- coordinator/subagent/session detail reducer
- tabs/detail/timeline/blocker 渲染

判断标准：

- `subagent-state.js` 负责状态归一化
- `app.js` 负责事件接入和 DOM 投影
- 后续继续对标 `opencode` 时，应优先扩 reducer 和 session 模型，而不是继续往 `app.js` 里堆分支

---

## 7. 对标后的剩余架构缺口

参考 `oh-my-openagent` 和 `opencode`，当前真正还值得继续补的不是“再支持一个事件名”，而是下面 4 个层级。

### 7.1 子会话仍是快照，不是持续流

当前已具备：

- `child_session.summary`
- `child_session.frames`
- `child_session.commits`
- `child_session.pending_queue`

但仍缺：

- 更完整的 session replay 语义
- richer tool/question/permission 细粒度 commit
- 更接近 `opencode` 的连续 session reducer
- 更清晰地区分
  - canonical raw task output
  - visible rendered output
  - model-visible child history
  三者的持久化边界
- replay cursor / after-seq / durable tail 这类真正接近 `opencode sessions.events(...)` 的消费边界

这意味着当前已经足够“观察多 subagent”，但还没到“把子会话当一等会话对象操作”。

### 7.2 parent wake 语义还弱于 `oh-my-openagent`

当前已具备：

- pending queue
- retry
- recent activity deferral
- visible revision sent 去重
- terminal completion / missing client fallback
- terminal retained resume 的“父会话已消费后丢弃”语义
  - 2026-06-27 本轮已补：
    - 当 terminal coordinator 已完成 `status/output/done` 可见发送
    - 但 `parent resume` 因父会话 recent activity 被延迟时
    - 如果延迟窗口内父会话后来又出现新的 activity
    - runtime 会把这次 retained resume 视为“父会话已自行消费当前结果”，直接丢弃，而不是冷却后再注入一条 `MSG_SOURCE_DELEGATE`
  - 这一步直接对齐的是 `oh-my-openagent` 里更强的
    - retained wake
    - consumed by parent
    - avoid duplicate notify/resume
    这组语义，而不是单纯的时间窗口重试

但仍缺：

- 更强的 history-based suppression
- admit-only / noReply / reply-required 保留语义
- 更严的 idle / output validity / unfinished work completion gate

这部分不是 UI 问题，而是并发 subagent 多了以后最容易演化成重复通知、重复 resume、错误完成判定的问题源头。

### 7.3 Web 仍偏 coordinator-first

当前 Web 已能显示：

- 多 coordinator
- 多 subagent
- 每个 agent 的 detail/timeline/blocker/pending queue

但仍缺：

- 像 `opencode` 一样彻底 session-first 的浏览模型
- 更自然的父/子 blocker 切换体验
- 更稳定的长时间 reconnect / restore 行为

这说明当前 Web 已可用，但还不是参考实现那种成熟交互层。

### 7.4 验证覆盖仍偏“关键路径回归”，还没形成完整矩阵

当前已经有：

- `./build-kbuild/agent --self-test`
- `scripts/dev/check-subagent-web-ui.js`
- `scripts/dev/check-websocket-http-subagent-replay.sh`
- `scripts/dev/check-websocket-interview-recovery.sh`
- `scripts/dev/check-websocket-staged-subagent.sh`

但还应该继续补：

- richer mixed blocker 组合回归
- reconnect / bootstrap / completion 交错场景
- 更长生命周期的 coordinator restore 验证

补充说明（2026-06-27 本轮）：

- staged `depends_on` 的真实 runtime 路径已经拿到直接证据，不再只是 self-test 间接证明。
- 当前 runtime log 已确认：
  - `dispatch_mode=staged`
  - 第三个子任务 `depends_on=scan-turn,scan-tooling`
  - 首次 coordinator 返回就是 `running_count=2, queued_count=1`
- 这说明当前后台调度器确实支持“依赖未满足时保持 queued，ready 子任务先启动”，不是把 batch 一律当 parallel 执行。
- 新增脚本：
  - `scripts/dev/check-websocket-staged-subagent.sh`
  - `scripts/dev/websocket-staged-directive.json`
- 当前这条 staged 回归脚本仍需继续打磨最终探针收尾条件，但 runtime 能力本身已经被真实日志证明。

---

## 8. 下一阶段最优顺序

如果目标是“最终项目可以完美支持多 subagent 任务”，建议按这个顺序推进，而不是继续散点修。

### 阶段 A：先继续补协议和验证

优先级最高，因为这决定后续改 UI 是否有稳定地基。

目标：

- child session 协议继续结构化
- mixed blocker 场景补回归
- staged / parallel / resume / completion 的真实链路补齐

### 阶段 B：把 `subagent-state.js` 继续向 session reducer 演进

目标：

- 子会话 detail 进一步从“事件推断”转向“session state 投影”
- 更明确地区分
  - snapshot adapter
  - event reducer
  - view selector

### 阶段 C：再补 parent wake 语义层

目标：

- 对齐 `oh-my-openagent` 中更强的 wake 判定与 dedupe 思想
- 降低重复 resume、重复完成通知、错误终态的概率

### 阶段 D：最后打磨 Web 交互

目标：

- 做成真正好用的多 subagent 可视化
- 而不是只把底层状态“显示出来”

---

## 9. 最终目标架构

如果以 `opencode + oh-my-openagent` 为参考上限，`daima-agent` 的最终态不应该是“现在这套 coordinator 面板再补几个事件”，而应该是下面这套分层。

### 9.1 总原则

1. `coordinator` 只负责编排，不负责 UI 真相
2. `child_session` 才是前后端共享的一等对象
3. WebSocket 和 HTTP 只提供 session-first 数据，不再长期并存两套语义中心
4. parent wake 只负责：
   - 对父会话何时可见
   - 对父会话何时 resume
   - 对重复通知何时抑制
5. Web 只维护一份 reducer 状态，不再页面层拼第二份 coordinator/event/blocker store

### 9.2 目标分层

#### A. Orchestration 层

目录中心：

- `drivers/tool/tool_delegate*.c`
- `kernel/tooling/delegate/delegate_task_store.c`
- `kernel/tooling/delegate/delegate_task_runtime.c`

职责：

- 解析 `delegate_task` 请求
- 创建 `task/coordinator`
- 处理 `parallel/staged/depends_on`
- 推进 background child run 生命周期

边界要求：

- 不直接面向 Web 组织 UI 字段
- 不直接散落 websocket 发送
- 不直接承担 parent merge 文案逻辑

#### B. Session Projection 层

目录中心：

- `kernel/tooling/delegate/delegate_task_projection.c`
- `kernel/tooling/delegate/delegate_session_json.c`
- `drivers/memory/session_store_file.c`

职责：

- 把 child runtime activity 投影到统一 `child_session`
- 统一维护：
  - `frames`
  - `commits`
  - `history`
  - `pending_queue`
  - `latest_frame`
  - `seq/id`
- 统一导出 HTTP/WS 共享的 session snapshot

边界要求：

- 这是多 subagent 可视化的唯一数据真相层
- 任何展示字段都应从这里派生，而不是再去读 event log 拼第二份

#### C. Parent Wake / Visibility 层

目录中心：

- `kernel/tooling/delegate/delegate_parent_wake.c`

职责：

- drain changed coordinator
- 处理 visible revision / retry / defer / retained resume
- 决定：
  - 是否向 Web 可见
  - 是否向父会话注入 resume
  - 是否丢弃重复 wake

边界要求：

- 只处理“何时发送/恢复”，不重新生成业务状态
- 不自己再维护一套 child session 语义
- 所有对外消息都尽量复用 projection 层导出的标准 session snapshot

#### D. Parent Turn Merge 层

目录中心：

- `kernel/turn/turn_entry.c`

职责：

- 消费 `MSG_SOURCE_DELEGATE`
- 以本地规则生成父会话汇总
- 只做 parent-facing merge，不回写子会话状态真相

边界要求：

- 这是 parent UX 层，不是子会话状态层
- 不能反过来污染 `child_session`

#### E. Web Transport / Reducer 层

目录中心：

- `spiffs_data/web/subagent-event-adapter.js`
- `spiffs_data/web/subagent-state-core.js`
- `spiffs_data/web/subagent-state-reducer.js`
- `spiffs_data/web/subagent-state-selectors.js`

职责：

- websocket/http payload 适配
- 统一 reducer
- selector 导出：
  - session list
  - selected detail
  - blockers
  - summary

边界要求：

- 这里只有一份状态树
- `app.js` 只做 DOM 投影和交互事件绑定
- “当前选中的 subagent/session”必须由 selector 决定，而不是页面层 ad-hoc 状态

#### F. Web View 层

目录中心：

- `spiffs_data/web/subagent-coordinator-view.js`
- `spiffs_data/web/subagent-detail-view.js`
- `spiffs_data/web/app.js`

职责：

- 渲染 session list / detail / blocker / parent interactive UI

边界要求：

- View 组件不拥有业务真相
- coordinator view 只是 orchestration 辅助视图，不再是 UI 主语
- 真正一级对象应该是 child session/detail

### 9.3 最终用户视角

最终用户应该感知到的不是“有几个 coordinator 在跑”，而是：

- 我有哪些 subagent session
- 每个 subagent 现在在干嘛
- 哪个 subagent 卡在权限/提问
- 哪个 subagent 已完成，并给了什么结论
- 父会话什么时候因为子任务完成而恢复

也就是说：

- `coordinator` 是运行时编排对象
- `child_session` 是用户交互对象

这就是要对齐 `opencode` 的地方。

---

## 10. 当前目录边界建议

为了支撑上面的最终态，当前仓库里和多 subagent 最相关的目录建议固定为下面这套职责，不再继续混放。

### 10.1 后端

`kernel/tooling/delegate/`

- `delegate_task_store.*`
  - task/coordinator registry
  - revision / wake metadata
- `delegate_task_projection.*`
  - child session 投影
- `delegate_session_json.*`
  - session snapshot export
- `delegate_parent_wake.*`
  - websocket flush / parent resume gate
- `delegate_task_runtime.*`
  - background launch / staged dispatch / runtime integration
- `delegate_task_query.*`
  - 面向外部查询的只读 helper
- `delegate_turn_directive.*`
  - interview / directive -> batch request bridge

要求：

- 新增多 subagent 行为时，优先放进上述现有边界
- 避免再把 delegate 逻辑散回 `tool_delegate.c`、`turn_entry.c`、`ws_http_helpers.c`

### 10.2 Web

`spiffs_data/web/`

- `subagent-event-adapter.js`
  - 把 WS/HTTP payload 变成 reducer action
- `subagent-state-core.js`
  - normalize / merge helper / invariant
- `subagent-state-reducer.js`
  - 唯一状态变更入口
- `subagent-state-selectors.js`
  - session-first selector
- `subagent-coordinator-view.js`
  - coordinator 辅助视图
- `subagent-detail-view.js`
  - child session 一级 detail 视图
- `app.js`
  - wiring / DOM mount / input handling

要求：

- 不再把 reducer 逻辑塞回 `app.js`
- 不再让 coordinator 统计逻辑和 detail/blocker 细节渲染互相缠绕

---

## 11. 阶段化实施方案

下面这套顺序不是“比较稳妥”，而是按最终态倒推后的最优顺序。

### 阶段 1：确立 session-first 单一真相

目标：

- HTTP / WS / self-test 都基于同一 `child_session` 导出层
- coordinator snapshot 不再携带额外独占语义
- 所有 detail/blocker/history/commit 都能从 session projection 解释

完成标准：

- 不再出现“HTTP 有，WS 没有”或“WS 新字段绕过 reducer”的分叉
- `child_session` 的 `seq/id/history/frames/commits/pending_queue` 在 trim/reconnect 后仍单调正确

### 阶段 2：把 Web 彻底收敛到 reducer + selector

目标：

- `app.js` 不再维护第二份业务状态
- session list/detail/blocker 以 selector 为唯一来源
- coordinator 视图降级为辅视图

当前进展补充：

- 已新增 page-level 的 unified restore contract：
  - `session-restore.js` 现在优先调用 `restoreSessionState(chatId, options)`
  - 旧的 `history + subagent snapshot` 双通道只保留为兼容回退
- `app.js` 已通过 `ensureSessionRestore()` 提供这个统一入口
  - page 层开始按“恢复一个 session 的完整视图状态”思考
  - 而不再把“history 恢复”和“subagent 恢复”视为两个平级流程
- 这一步还不是最终形态：
  - 当前 unified restore 仍是对旧链路的高层包装
  - 但边界已经收敛成 session-first contract，后续可以继续把 transport replay 吸进同一入口

完成标准：

- 手动刷新、WS 重连、HTTP bootstrap 后看到的是同一份 detail/session 结果
- 关闭/打开面板不会丢失已知 child session 状态

### 阶段 3：补齐 parent wake correctness

目标：

- 对齐 `oh-my-openagent` 的 async gate / retained wake / failure wake / noReply 语义
- 明确区分：
  - 仅可见通知
  - 需要父会话回复的 resume
  - 已被父会话消费的 retained wake

完成标准：

- 不重复 resume
- 不吞 failure wake
- 父会话 recent activity 不会错误消耗 reply-required wake

### 阶段 4：补更完整 child session stream

目标：

- 从“快照窗口”推进到“更像真实 session replay”
- 让 child assistant/reasoning/tool/request 生命周期更完整地可见

完成标准：

- detail 视图能稳定解释一个中长生命周期 subagent 的关键步骤
- reconnect 后不会只剩最终摘要，早期关键 activity 仍可恢复

### 阶段 5：最终交互层打磨

目标：

- session-first UI 成熟化
- parent/child blocker 切换自然
- parallel/staged 场景下用户能直观看懂当前整体进度

完成标准：

- 用户不需要理解 coordinator 概念，也能使用多 subagent
- coordinator 只在需要排障或查看编排元信息时才成为显式概念

---

## 12. 每阶段验收矩阵

### 12.1 协议与后端

- `./build-kbuild/agent --self-test`
- 覆盖：
  - parallel
  - staged
  - pending request
  - history window trim
  - seq monotonic
  - parent wake retain/drop/retry

### 12.2 Web reducer

- `node scripts/dev/check-subagent-web-ui.js`
- 覆盖：
  - hydrate
  - stale websocket snapshot
  - blocker restore
  - selected detail continuity

### 12.3 真实 websocket 链路

- `scripts/dev/check-websocket-http-subagent-replay.sh`
- `scripts/dev/check-websocket-interview-recovery.sh`
- `scripts/dev/check-websocket-staged-subagent.sh`

应覆盖：

- parent interview -> resume -> multi subagent
- staged depends_on
- reconnect / bootstrap / done
- child session history visible after completion

### 12.4 最终目标判定

只有下面四点都满足，才能说“完美支持多 subagent”：

1. 后端可以稳定调度 parallel + staged 子任务
2. parent wake 不会重复/丢失/错误消费终态恢复
3. Web 以 child session 为一级对象稳定恢复与展示
4. child session 能在真实运行里提供足够完整的可解释轨迹，而不只是最终摘要
