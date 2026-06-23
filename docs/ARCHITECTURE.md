# Daima Agent 代码框架文档

本文档是代码框架的阅读入口。
目标是回答三件事：

- 代码主链在哪里
- 每个阶段由哪些文件负责
- 想改某类行为时，应该先看哪里

同时，这份文档会明确写出一条完整业务链：

- 用户输入如何进入系统
- Agent 如何做决策
- LLM 与工具如何执行
- 最终结果如何返回给客户
- 若模型显式选择 `delegate_task`，subagent 协作如何插入主链

代码基线：`C11 + Kbuild`，单二进制 Agent，主链位于 `kernel/`。

---

## 1. 代码框架总览

仓库可以按下面的代码职责理解：

```text
daima-agent/
├── init/            启动引导与初始化链
├── kernel/          默认 turn 主链与调度
├── drivers/         能力层：tool / llm / channel / memory / skill
├── ipc/             bus、IPC、core_task
├── fs/ net/ lib/    基础设施
├── spiffs_data/     运行时配置、技能、静态资源
└── docs/            说明文档
```

默认框架的核心约束：

- 只有一条默认 turn 主链：`kernel/loop.c -> kernel/turn_entry.c -> kernel/turn_prepare.c -> kernel/turn_pipeline.c -> kernel/turn_finish.c`
- `drivers/` 是能力层，不反向定义主流程
- `subagent` 是主链可调用能力，不是第二条隐式主链
- skill 摘要与 skill tool 生命周期分离

---

## 2. 目录职责

### 2.1 `init/`

负责进程级启动，不放业务 turn 逻辑。

优先阅读：

- `init/main.c`
- `init/bootstrap.c`

这里完成：

- agent home / spiffs 目录准备
- `do_basic_setup()` 4 级初始化链
- 主服务启动

### 2.2 `kernel/`

这是默认框架唯一主链，负责：

- 单回合编排
- intent / role / plan / model 决策
- prompt 与 message 准备
- LLM 执行与工具循环
- turn 收尾
- async resume
- subagent 调度

如果你要理解“消息进来以后系统到底怎么跑”，先读 `kernel/`。

### 2.3 `drivers/`

能力层目录，不负责定义默认主链顺序。

主要子目录：

- `drivers/tool/`：工具定义、工具运行时、terminal/files/todo 等工具
- `drivers/llm/`：OpenAI / Anthropic 兼容 payload、模型回退、LLM proxy
- `drivers/channel/`：websocket / feishu / vector 等通道
- `drivers/memory/`：session store、facts、summary
- `drivers/skill/`：skill loader、summary、meta、tool activation

### 2.4 `ipc/`

负责消息总线、核间 IPC、异步执行核通信。

### 2.5 `spiffs_data/`

运行时数据和静态资源目录，包括：

- skill 内容
- 配置
- Web UI 静态资源
- 其他运行期读写依赖

---

## 3. 启动框架

### 3.1 入口

进程入口在：

- `init/main.c`

关键流程是：

1. 准备运行目录
2. 初始化 SPIFFS 布局
3. 执行 `do_basic_setup()`
4. 启动各类服务

### 3.2 初始化链

`do_basic_setup()` 在：

- `init/bootstrap.c`

固定按四级执行：

| 级别 | 作用 |
|------|------|
| `core` | message bus、IPC、extensions |
| `postcore` | memory / session store |
| `subsys` | cron / heartbeat / proxy / skill loader |
| `device` | channel bus / llm bus / executor core / memory core |

这是启动依赖闭包，不是可替换的插件式启动图。

### 3.3 服务启动

启动完成后，主服务依次拉起：

- `llm_proxy_init()`
- `tool_builtin_bus_init()`
- `agent_loop_init()`
- `channel_router_start()`
- `agent_loop_start()`
- `ws_server_start()`

---

## 4. 默认 Turn 主链

### 4.1 外层循环

文件：

- `kernel/loop.c`

`agent_loop_task()` 只做两件事：

1. 轮询 `agent_turn_resume_poll()`
2. 从 inbound bus 取消息并交给 `agent_turn_process_new_message()`

这里不做 intent / role / plan / prompt 的业务判断。

---

## 5. 端到端主链

这一节按代码真实顺序串起主链：

- 用户输入
- 前置过滤与决策
- 执行前准备
- interview 或执行
- 收尾
- outbound 回传

`subagent` 协作不是每轮必经阶段，而是执行阶段中的一个可选能力分支。

### 5.1 用户输入进入系统

用户输入首先经由 channel 进入 inbound bus。

WebSocket 场景下，关键文件是：

- `drivers/channel/gateway/ws_client.c`

其他 channel 也遵循同样模式：把收到的消息封装成 `struct message`，推入 inbound bus。

总线入口在：

- `ipc/bus.c`

关键函数：

- `message_bus_push_inbound(...)`
- `message_bus_pop_inbound(...)`

随后主循环在：

- `kernel/loop.c`

执行：

1. `agent_turn_resume_poll()`
2. `message_bus_pop_inbound(&msg, 0)`
3. `agent_turn_process_new_message(&msg)`

也就是说，用户输入真正进入 Agent 主链的入口函数是：

- `agent_turn_process_new_message(...)`

### 5.2 Agent 决策

Agent 决策发生在：

- `kernel/turn_entry.c`
- `kernel/turn_decision.c`

关键链路如下：

1. `agent_turn_handle_self_test_command(msg)`
2. `agent_turn_validate_inbound_message(msg)`
3. `agent_turn_io_init(&io)`
4. `agent_turn_decision_reset(&decision)`
5. `agent_turn_decide(msg, &decision)`

`agent_turn_decide(...)` 内部做三类核心决策：

1. 意图决策
   - `intent_gate_classify(...)`
   - 结果写入 `msg->intent`

2. 角色决策
   - `agent_roles_for_intent(...)`
   - 根据 intent 选择角色集合
   - 若 plan 已生成且 reviewed，则可能切到更适合执行的角色

3. 计划与模型决策
   - `plan_review_generate(...)`
   - `agent_turn_resolve_model(...)`

这里要明确区分 4 个概念：

- `intent`
  - 只负责任务分类，如 `qa / implement / investigate / fix / open`
- `role`
  - 负责决定该由谁执行，如 `FAST / PLANNER / EXECUTOR / REVIEWER`
- `role_model_map`
  - 负责把角色映射到 provider/profile
- `active_provider`
  - 作为角色路由未命中时的兜底 provider

规则如下：

- `intent` 不直接参与模型路由
- 主 agent 先根据 `intent` 推导 `role`
- 主 agent 再按 `role_model_map[active_role]` 选模型
- 若角色没有命中路由，则回退到 `active_provider`

可以压成一句：

- `intent` 决定做什么
- `role` 决定谁来做
- `role_model_map` 决定谁用哪个模型
- `active_provider` 负责兜底

`category_routing` 没有单独的 `enabled` 开关。
只要存在角色路由配置或默认路由可构建，角色选模就会生效。

所以“Agent 决策”不是单一函数，而是这三件事的组合：

- 这条消息属于什么 intent
- 应该用哪个 role 来执行
- 是否需要生成结构化 plan，以及最终路由到哪个模型

### 5.3 执行前准备

决策完成后，主链进入执行前准备。

关键调用在：

- `kernel/turn_entry.c`

顺序是：

1. `agent_turn_prepare(...)`
2. `agent_turn_append_role_prompt(...)`
3. `agent_turn_inject_team_guidance(...)`
4. `tool_bus_tools_json_for_channel(...)`
5. `agent_turn_resolve_model(...)`

这一段的结果是三份关键输入：

- `system_prompt`
- `messages`
- `tools_json`

准备阶段内部再拆成：

- `turn_prompt_build.c`
  - 组装最终 `system_prompt`
- `turn_message_build.c`
  - 组装 `history_json` 与本轮 `messages`

### 5.4 interview 或执行

执行入口在：

- `agent_run_prepared_turn(...)`

文件：

- `kernel/turn_pipeline.c`

显式顺序是：

1. `agent_turn_try_interview(...)`
2. `agent_turn_run(...)`
3. `agent_finalize_turn(...)`

#### 5.4.1 interview 短路

文件：

- `kernel/turn_interview.c`

它负责在正式执行前判断：

- 当前需求是否过于模糊
- 是否应先向用户追问澄清问题

命中后：

- 直接产生 final text
- 不进入 LLM 工具循环

#### 5.4.2 LLM 工具循环

文件：

- `kernel/turn_run.c`

这是执行阶段的核心循环：

1. 调用 LLM
   - `llm_chat_tools_with_model(...)`
   - 或 `model_fallback_chat_with_fallback(...)`

2. 如果模型直接返回文本
   - 结束执行

3. 如果模型返回 tool calls
   - 进入工具执行

4. 把 assistant content + tool_result content 追加回 `messages`
5. 继续下一轮 LLM

也就是说，真正的执行不是“一次 LLM 调用”，而是：

- `LLM -> tools -> LLM -> tools -> ... -> final text`

#### 5.4.3 工具执行

文件：

- `kernel/turn_exec.c`
- `drivers/tool/tool_runtime.c`
- `drivers/tool/tool_terminal_exec.c`

关键过程：

1. `agent_turn_build_assistant_content(&resp)`
   - 把模型本轮输出转成 assistant content blocks

2. `agent_turn_build_tool_results(...)`
   - 逐个执行工具调用
   - 收集 tool output
   - 组装 `tool_result` blocks

3. tool output 重新写回 `messages`
   - 供下一轮模型继续推理

这就是代码里的默认执行主干。

#### 5.4.4 可选的 subagent 协作分支

`subagent` 不在 `turn_entry` 或 `turn_pipeline` 里被主链强制调用。
它只会在执行阶段中，由模型显式调用工具时进入。

相关文件：

- `drivers/tool/tool_delegate.c`
- `kernel/sched/core.c`
- `kernel/sched/sched.h`

真实语义是：

- 主链先把 `delegate_task` 暴露在工具集合中
- 模型如果判断任务复杂，才会调用它
- 调用后进入 `PLANNER / EXECUTOR / REVIEWER` 协作
- 每个 subagent 启动时会先按自己的角色从 `role_model_map` 选模型
- 若角色没有命中路由，再回退到当前全局模型
- 协作结果再回到当前 turn 的工具结果链中

所以按代码实际，`subagent` 是“执行阶段中的可选协作分支”，不是默认主链的固定阶段。

### 5.5 收尾

执行完成后进入：

- `agent_finalize_turn(...)`
- `agent_turn_finish(...)`

收尾分成两段：

1. reply 处理
   - `kernel/turn_reply.c`

2. post actions
   - `kernel/turn_post.c`

reply 处理负责：

- cancelled 路径
- success 路径
- error 路径
- Ralph warning
- session save
- outbound queue

post actions 负责：

- inbound cleanup
- skill tool unregister
- todo progress record
- recovery clear
- context compaction dispatch

### 5.6 返回客户

最终返回客户不是在 `turn_finish` 里直接发 socket，而是先推入 outbound bus。

关键函数在：

- `kernel/turn_persist.c`

具体是：

- `agent_turn_queue_outbound_text(...)`
  - 调 `message_bus_push_outbound(&out)`

之后由：

- `kernel/channel_router.c`

启动的 `dispatch_outbound_task()` 持续消费 outbound bus：

1. `message_bus_pop_outbound(&msg, UINT32_MAX)`
2. `channel_runtime_dispatch_outbound(&msg)`
3. `agent_cleanup_outbound_msg(&msg)`

真正按 channel 类型分发的地方在：

- `kernel/channel_runtime.c`

例如 WebSocket 最终会走回：

- `drivers/channel/gateway/ws_client.c`

把 response 序列化成 websocket JSON，再发给前端客户端。

因此，“最后返回客户”的真实链路是：

1. `turn_reply.c` 生成最终文本
2. `turn_persist.c` 推入 outbound bus
3. `channel_router.c` 消费 outbound bus
4. `channel_runtime.c` 按 channel 路由
5. `drivers/channel/...` 发送给真实客户端

### 5.7 端到端顺序总表

把整条链压成一行就是：

1. 用户通过 channel 发消息
2. channel 封装 `struct message` 推入 inbound bus
3. `kernel/loop.c` 取消息
4. `agent_turn_process_new_message(...)`
5. Agent 做 intent / role / plan / model 决策
6. `agent_turn_prepare(...)` 组织 prompt / history / messages
7. role prompt + team guidance 注入
8. `agent_run_prepared_turn(...)`
9. interview 命中则直接澄清
10. 否则进入 `agent_turn_run(...)`
11. LLM 返回文本或 tool calls
12. 如有 tool calls，执行工具并把结果回灌到 messages
13. 循环直到得到 final text
14. `agent_turn_finish(...)` 做回复、保存、清理
15. `message_bus_push_outbound(...)`
16. `channel_router` 分发 outbound
17. 具体 channel 把结果发送给客户

注意：

- 只有在执行阶段模型显式调用 `delegate_task` 时，才会插入 subagent 协作
- 所以“用户输入 -> Agent 决策 -> 分工 -> 执行 -> 返回客户”不是默认固定主链
- 更精确的代码表达是：
  - `用户输入 -> 前置过滤与决策 -> 准备 -> interview/执行 -> 收尾 -> outbound 回传`
  - `subagent` 是执行阶段的可选能力分支

### 4.2 单回合入口

文件：

- `kernel/turn_entry.c`

入口函数：

- `agent_turn_process_new_message(struct message *msg)`

显式顺序：

1. `turn_gate` 处理 internal control / `!test`
2. 初始化本轮临时 I/O
3. intent 分类
4. role 选择
5. plan 生成
6. `agent_turn_prepare()`
7. role prompt 注入
8. team guidance 注入
9. model route 解析
10. `agent_run_prepared_turn()`

也就是说，执行前的默认业务决策都在 `turn_entry.c` 这条链路里完成。

### 4.3 本轮临时 I/O

文件：

- `kernel/turn_io.c`

负责同步回合使用的：

- `system_prompt`
- `history_json`
- `messages`

它不负责异步恢复。

### 4.4 准备阶段

文件：

- `kernel/turn_prepare.c`
- `kernel/turn_prompt_build.c`
- `kernel/turn_message_build.c`

边界固定如下：

- `turn_prepare.c`
  - prepare orchestrator
- `turn_prompt_build.c`
  - system prompt 注入链
  - rules / summary / facts / compaction recovery / session recovery / todo / runtime context / channel policy / plan
- `turn_message_build.c`
  - history JSON 读取
  - 本轮 message 组装
  - synthetic event 包装
  - vision content 组装

主入口保持不变：

- `agent_turn_prepare(...)`

### 4.5 执行阶段

文件：

- `kernel/turn_pipeline.c`
- `kernel/turn_interview.c`
- `kernel/turn_run.c`
- `kernel/turn_exec.c`

边界固定如下：

- `turn_pipeline.c`
  - prepared turn execute orchestration
  - finalize 包装
- `turn_interview.c`
  - clarification/interview short-circuit
- `turn_run.c`
  - LLM 工具循环
- `turn_exec.c`
  - assistant content / tool_result content 组装
  - tool runtime 执行

`agent_run_prepared_turn()` 顺序：

1. `agent_turn_try_interview(...)`
2. 若命中 interview，直接返回 final text
3. 否则进入 `agent_turn_run(...)`
4. 统一 `agent_finalize_turn(...)`

### 4.6 收尾阶段

文件：

- `kernel/turn_finish.c`
- `kernel/turn_reply.c`
- `kernel/turn_post.c`
- `kernel/turn_persist.c`

边界固定如下：

- `turn_finish.c`
  - finish orchestrator
- `turn_reply.c`
  - cancelled / success / error reply path
  - Ralph warning
  - session save
  - outbound queue
- `turn_post.c`
  - inbound cleanup
  - skill tool unregister
  - todo progress record
  - compaction/session recovery clear
  - dispatch context compaction
- `turn_persist.c`
  - session store write
  - outbound message build
  - error reply helper

主入口保持不变：

- `agent_turn_finish(...)`

### 4.7 异步恢复快照

文件：

- `kernel/turn_context.c`
- `kernel/turn_resume.c`

这部分只服务 async resume。

保存的是：

- `messages` 快照
- `system_prompt`
- iteration
- cancel token
- pending task 信息

它不是当前同步回合的临时上下文容器。

---

## 6. Turn 阶段辅助模块

这些文件不直接执行 LLM，但属于默认 turn 主链的重要组成：

| 文件 | 作用 |
|------|------|
| `kernel/turn_gate.c` | `!test` / internal control 前置过滤 |
| `kernel/turn_decision.c` | intent / role / plan / model 决策 |
| `kernel/turn_prompt.c` | role prompt / team guidance 注入 |
| `kernel/router.c` | role / intent 到模型的路由 |
| `kernel/todo.c` | todo enforcer |
| `kernel/recovery.c` | session crash recovery |
| `kernel/compaction.c` | compaction recovery snapshot / inject |
| `kernel/ralph.c` | Ralph continuation logic |

---

## 7. Tool 执行框架

### 6.1 工具声明与能力层

工具定义主要在：

- `drivers/tool/`

常见入口包括：

- `tool_terminal_exec.c`
- `tool_runtime.c`
- `tool_files.c`
- `tool_todo.c`
- `tool_delegate.c`

### 6.2 Turn 内工具调用链

默认链路是：

1. LLM 在 `turn_run.c` 返回 tool calls
2. `turn_exec.c` 构造 assistant content
3. `turn_exec.c` 调 `tool_runtime_execute_call(...)`
4. tool output 被包装为 `tool_result`
5. `messages` 继续回送给下一轮 LLM

如果你在排查工具调用行为，优先看：

- `kernel/turn_exec.c`
- `drivers/tool/tool_runtime.c`
- `drivers/tool/tool_terminal_exec.c`

### 6.3 异步执行核

大工具/异步执行相关：

- `kernel/executor_core.c`
- `kernel/memory_core.c`
- `kernel/turn_dispatch.c`

它们负责把部分动作从主循环解耦出去。

---

## 8. LLM Payload 框架

文件：

- `drivers/llm/llm_proxy.h`
- `drivers/llm/llm_openai_payload.c`
- `drivers/llm/llm_anthropic_payload.c`
- `drivers/llm/model_fallback.c`

职责边界：

- `llm_proxy`
  - 统一入口
- `llm_openai_payload.c`
  - OpenAI-compatible request/response build & parse
- `llm_anthropic_payload.c`
  - Anthropic-compatible request/response build & parse
- `model_fallback.c`
  - 主模型失败后的 fallback 链

如果你在排查：

- request body 格式
- tool_use / tool_result 协议
- thinking / reasoning 块
- 模型 fallback

先从这里读。

---

## 9. Memory / Summary / Facts 框架

文件：

- `drivers/memory/session_store.c`
- `drivers/memory/session_store_file.c`
- `drivers/memory/session_store_file_summary.c`
- `drivers/memory/session_store_file_facts.c`

职责：

- 历史消息 JSONL 存储
- summary 读写
- facts 读写
- session record 枚举

与 turn 主链的关系：

- `turn_message_build.c` 读 history
- `turn_prompt_build.c` 读 summary / facts
- `turn_persist.c` 写 user / assistant turn
- `context_ops.c` 生成 summary / facts

---

## 10. Skill 框架

文件：

- `drivers/skill/skill_loader.c`
- `drivers/skill/skill_summary.c`
- `drivers/skill/skill_meta.c`
- `drivers/skill/skill_tools.c`

当前语义必须明确区分：

- skill summary
  - 给模型看的能力描述
- skill tools
  - 真正注册到 tool bus 的工具

所以：

- 构建了 skill 摘要
  - 不代表工具已激活
- `skill_tools_activate_selected(...)`
  - 才会显式注册工具
- `skill_tools_unregister_all()`
  - 在 turn 收尾统一回收

---

## 11. Subagent 框架

文件：

- `drivers/tool/tool_delegate.c`
- `kernel/sched/core.c`
- `kernel/sched/sched.h`

代码约束：

- `subagent` 是主链能力，不是替代执行链
- 入口只认 `delegate_task`
- 调度只走 `kernel/sched`
- 默认是 `PLANNER / EXECUTOR / REVIEWER`
- 不允许递归委托

---

## 12. 阅读路径建议

### 11.1 想理解“消息从哪里进来”

按这个顺序读：

1. `init/main.c`
2. `kernel/loop.c`
3. `kernel/turn_entry.c`
4. `kernel/turn_prepare.c`
5. `kernel/turn_pipeline.c`
6. `kernel/turn_finish.c`

### 11.2 想改 prompt / message 组织

先看：

- `kernel/turn_prompt_build.c`
- `kernel/turn_message_build.c`
- `kernel/turn_prompt.c`

### 11.3 想改工具调用行为

先看：

- `kernel/turn_run.c`
- `kernel/turn_exec.c`
- `drivers/tool/tool_runtime.c`
- `drivers/tool/tool_terminal_exec.c`

### 11.4 想改会话总结 / facts / 压缩

先看：

- `kernel/context_ops.c`
- `drivers/memory/session_store_file_summary.c`
- `drivers/memory/session_store_file_facts.c`
- `kernel/compaction.c`

### 11.5 想改恢复 / 收尾行为

先看：

- `kernel/recovery.c`
- `kernel/turn_reply.c`
- `kernel/turn_post.c`
- `kernel/turn_persist.c`

---

## 13. 框架约束

阅读和改代码时，默认按下面理解：

- 默认主链只在 `kernel/`
- `drivers/` 负责能力，不负责偷改主链顺序
- `turn_prepare / turn_pipeline / turn_finish` 是固定阶段入口
- 阶段内细分落到 `turn_*` 子模块
- `turn_context` 只存 async resume snapshot
- 同步回合临时资源只由 `turn_io` 管理
- skill summary 不等于 skill tools activated
- `subagent` 只允许走 `delegate_task + kernel/sched`

维护这份文档时，优先同步以下内容：

- 默认主链顺序
- 阶段内边界
- 关键入口函数
- 目录职责
