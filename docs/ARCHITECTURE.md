# Daima Agent 架构文档

Daima Agent 是一个 `C11 + Kbuild` 的单二进制 AI Agent。这个工程当前的架构目标不是“尽量可扩展”，而是“主链唯一、状态显式、能力边界清晰、运行路径可读”。

---

## 1. 目标与原则

### 1.1 架构目标

这套框架当前追求四件事：

- 只有一条默认主链，读代码时能直接看到真实流程
- 每个能力都有明确入口，不能靠旁路偷偷接管主流程
- 默认执行链要尽量短，避免多层抽象和双轨逻辑
- 复杂能力可以保留，但必须挂在清晰的能力边界上

### 1.2 核心原则

#### 唯一主链

默认回合流程只在 `kernel/`。  
任何 correctness 都必须从主链直接读出来，不能依赖扩展模块、钩子系统或隐式注册副作用。

#### 显式状态

回合相关状态必须由主链显式生成、传递和消费。  
如果一个状态会影响意图、计划、角色、模型或收尾，它就不应该藏在旁路机制里。

#### 能力独立

`subagent`、skill tools、memory、channel、llm` 都是能力，不是主链。  
它们可以被主链调用，但不能反过来定义主链。

#### 默认最小化

默认构建只保留必须的执行路径。  
预留目录、可选能力、实验入口可以存在，但默认不能参与主流程。

---

## 2. 系统分层

当前工程可以按下面的层次理解：

```text
daima-agent/
├── init/            启动引导
├── kernel/          默认主链
├── drivers/         工具、模型、通道、memory、skill
├── ipc/             bus / message bus / core_task
├── fs/ net/ lib/    基础设施
├── spiffs_data/     运行时配置与技能数据
└── docs/            架构与说明文档
```

### 2.1 `kernel/`

`kernel/` 是唯一主链，负责：

- turn 流水线
- 意图、角色、计划、模型路由
- interview、ralph、todo、recovery
- subagent 调度
- 多核执行协调

### 2.2 `drivers/`

`drivers/` 是能力层，不是主链层。主要包括：

- `drivers/tool/`：工具总线与工具实现
- `drivers/llm/`：模型协议
- `drivers/channel/`：消息通道
- `drivers/memory/`：持久化
- `drivers/skill/`：skill 摘要、元数据、工具激活

## 3. 启动链

### 3.1 运行时准备

`bootstrap_prepare_runtime()` 负责：

- `paths_init()`：初始化路径
- `ensure_spiffs_layout()`：创建运行时目录
- `runtime_config_init()`：加载配置

### 3.2 4 级手动初始化链

`do_basic_setup()` 固定按以下顺序执行：

| 级别 | 调用链 | 职责 |
|------|--------|------|
| `core` | `message_bus_init` → `core_ipc_init` | 消息总线、IPC |
| `postcore` | `memory_store_init` → `session_store_init` | 持久化 |
| `subsys` | `cron_service_init` → `heartbeat_init` → `http_proxy_init` → `skill_loader_init` | 子系统能力 |
| `device` | `bus_init` → `bus_channel_register_all` → `bus_llm_register_all` → `executor_core_start` → `memory_core_start` | 总线与异步执行核 |

这个顺序表达的不是“模块分层美观”，而是“主链运行所需依赖的最小闭包”。

### 3.3 服务启动

运行时服务再按顺序拉起：

- `llm_proxy_init()`
- `tool_builtin_bus_init()`
- `agent_loop_init()`
- `channel_router_start()`
- `agent_loop_start()`
- `ws_server_start()`

---

## 4. 回合主链

### 4.1 入口：`kernel/loop.c`

`process_new_message()` 是默认回合入口。它按显式顺序完成：

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

这一步的意义是：  
主链在进入执行前，就把所有默认决策都算完，不给旁路留主导空间。

### 4.2 准备阶段：`kernel/turn_prepare.c`

这一层负责：

- 加载会话历史
- 构建 system prompt
- 组织 messages
- 注入 plan

它的职责是“把执行所需上下文准备完整”，而不是替执行阶段做决策。

### 4.3 执行阶段：`kernel/turn_pipeline.c`

当前执行链只有三步：

1. `agent_turn_maybe_interview()`
2. `agent_turn_run()`
3. `agent_finalize_turn()`

关键约束：

- `Prometheus interview` 已前移到主链
- 模型路由由主链显式下传
- 默认执行路径不再经过 hook 或扩展替换

### 4.4 收尾阶段：`kernel/turn_finish.c`

这一层负责：

- 最终文本分发
- 错误回复构造
- 会话持久化
- Ralph Loop 警告追加
- recovery / todo / compaction 清理
- `skill_tools_unregister_all()`

这保证了“能力可以临时激活，但生命周期必须统一回收”。

---

## 5. Subagent 机制

### 5.1 设计定位

`subagent` 是默认主链支持的能力，但它不是另一条主链。

它存在的目的只有一个：  
把复杂任务拆给多个职责明确的 agent 并行处理。

### 5.2 唯一入口

当前 `subagent` 只保留一条正路：

- 工具入口：`drivers/tool/tool_delegate.c`
- 调度核心：`kernel/sched/core.c`
- 调度接口：`kernel/sched/sched.h`

也就是说：

- 复杂任务并行仍支持
- `delegate_task` 仍可调度 `PLANNER / EXECUTOR / REVIEWER`
- 不允许通过旁路、替代执行链等方式偷偷启动另一套流程

### 5.3 主链关系

主链只负责：

- 决定何时进入 LLM 执行
- 提供 `delegate_task` 可用能力

是否使用 `subagent`，由模型在执行中显式选择。  
这让 `subagent` 成为一种能力，而不是架构上的暗门。

---

## 6. Skill 机制

### 6.1 设计定位

skill 子系统解决的是“如何向模型暴露外部能力描述”，不是“如何偷偷改工具表”。

### 6.2 职责拆分

当前 `drivers/skill/` 采用明确分工：

- `skill_loader.c`：技能系统入口
- `skill_summary.c`：只构建摘要文本
- `skill_meta.c`：元数据与路径解析
- `skill_tools.c`：显式激活 / 注销 skill tools

### 6.3 当前语义

- `skill_summary_build_for_channel()`：只产出摘要，不注册工具
- `skill_tools_activate_selected(...)`：按技能名显式激活工具
- `skill_tools_unregister_all()`：turn 收尾统一回收

这条边界很重要：

- “读到了 skill” 不等于 “skill 工具已可执行”
- “摘要构建” 和 “工具生命周期” 完全分离

---

## 7. 非目标与禁区

当前框架明确不追求下面这些东西：

### 7.1 不追求双轨主链

不再允许 `kernel` 和 `extensions` 同时像主链。  
默认执行路径必须只有一条。

### 7.2 不追求可劫持的扩展点

不再允许任何机制直接改写默认执行链。  
尤其不允许：

- 替换执行入口
- 隐式改模型路由
- 用旁路逻辑决定 correctness

### 7.3 不追求隐式工具注册

skill 摘要构建不能修改工具总线。  
所有工具变化都必须是显式生命周期动作。

---

## 8. 后续收敛方向

当前架构已经收掉了旧扩展层和 hook 对主链的干扰。后面还可以继续做两件事：

- 把 `kernel/state.*` 从进程级单例继续收紧到明确的 turn/loop 上下文对象
- 继续更新 `drivers/skill/AGENTS.md` 等知识文档，让所有说明文件与当前实现完全一致
