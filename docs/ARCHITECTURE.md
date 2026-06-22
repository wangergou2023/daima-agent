# Daima Agent 架构文档

Daima Agent 是一个 `C11 + Kbuild` 的单二进制 AI Agent。当前默认框架已经收敛成一条明确的 `kernel` 主链，`extensions/` 不参与默认执行路径，`subagent` 只走 `delegate_task + kernel/sched`。

---

## 当前框架概览

```text
daima-agent/
├── init/                    启动引导
├── kernel/                  唯一主链：turn、调度、上下文、规则
├── drivers/                 tool / llm / channel / memory / skill
├── ipc/                     bus / message bus / core_task
├── fs/ net/ lib/            基础设施
├── extensions/              预留目录，默认空装配
├── spiffs_data/             配置、skills、运行时数据
└── docs/                    架构与说明文档
```

现在的硬规则：

- 默认回合流程只认 `kernel/`
- `extensions/` 不承载任何默认主链步骤
- `subagent` 只认 `delegate_task + kernel/sched`
- skill 摘要与 skill 工具生命周期彻底分离

---

## 启动流程

### 运行时准备

`bootstrap_prepare_runtime()` 负责：

- `paths_init()`
- `ensure_spiffs_layout()`
- `runtime_config_init()`

### 4 级手动初始化链

`do_basic_setup()` 固定按以下顺序执行：

| 级别 | 调用链 | 职责 |
|------|--------|------|
| `core` | `message_bus_init` → `core_ipc_init` → `extensions_init` | 消息总线、IPC、预留扩展入口 |
| `postcore` | `memory_store_init` → `session_store_init` | 持久化 |
| `subsys` | `cron_service_init` → `heartbeat_init` → `http_proxy_init` → `skill_loader_init` | 子系统服务 |
| `device` | `bus_init` → `bus_channel_register_all` → `bus_llm_register_all` → `executor_core_start` → `memory_core_start` | 总线与异步执行核 |

`extensions_init()` 在默认构建下是空装配。

### 服务启动

- `llm_proxy_init()`
- `tool_builtin_bus_init()`
- `agent_loop_init()`
- `channel_router_start()`
- `agent_loop_start()`
- `ws_server_start()`

---

## 默认回合主链

### 入口：`kernel/loop.c`

`process_new_message()` 的默认顺序：

1. 处理内部控制消息与 `!test`
2. `agent_turn_state_reset()`
3. `intent_gate_classify()`
4. `agent_roles_for_intent()`
5. `plan_review_generate()`（仅 `IMPLEMENT` / `FIX`）
6. `agent_turn_prepare()`
7. 追加 role prompt
8. 注入 team guidance
9. 解析 model route
10. `agent_run_prepared_turn()`

这里已经没有 extension hook 参与主链决策。

### 准备：`kernel/turn_prepare.c`

负责：

- 会话历史加载
- system prompt 构建
- messages 组织
- plan 注入

### 执行：`kernel/turn_pipeline.c`

执行链现在只有三步：

1. `agent_turn_maybe_interview()`
2. `agent_turn_run()`
3. `agent_finalize_turn()`

关键点：

- `Prometheus interview` 已前移到主链
- 模型路由由主链显式下传
- 默认执行路径不再经过 `replace_run` / `before_run`

### 收尾：`kernel/turn_finish.c`

负责：

- 最终文本分发
- 错误回复构造
- 会话持久化
- Ralph Loop 警告追加
- recovery / todo / compaction 清理
- `skill_tools_unregister_all()`

---

## Subagent 主路径

`subagent` 能力仍然保留，但只保留一条正路：

- 工具入口：`drivers/tool/tool_delegate.c`
- 调度核心：`kernel/sched/core.c`
- 调度接口：`kernel/sched/sched.h`

也就是说：

- 复杂任务并行仍支持
- `delegate_task` 仍可调度 `PLANNER / EXECUTOR / REVIEWER`
- 不再允许通过 `extensions/` 或 hook 旁路接管主执行链

---

## Skill 与工具生命周期

当前 `drivers/skill/` 的职责边界：

- `skill_loader.c`：技能系统入口
- `skill_summary.c`：只构建摘要文本
- `skill_meta.c`：元数据与路径解析
- `skill_tools.c`：显式激活 / 注销 skill tools

接口语义：

- `skill_summary_build_for_channel()`：只产出摘要，不注册工具
- `skill_tools_activate_selected(...)`：按技能名显式激活工具
- `skill_tools_unregister_all()`：turn 收尾统一回收

---

## extensions 的当前定位

`extensions/` 现在只是一个预留目录。

当前要求：

- 默认构建不在这里放任何主链逻辑
- 新功能优先直接落 `kernel/`
- 如果将来要重新启用扩展，也不能影响默认 correctness

---

## 后续建议

- 继续把 `kernel/state.*` 从进程级单例收紧到明确的 turn/loop 上下文对象
- 如果不再需要预留扩展能力，可以继续删掉 `kernel/hooks.*`
- 更新 `kernel/AGENTS.md`、`drivers/skill/AGENTS.md`，让知识文档与当前实现完全一致
