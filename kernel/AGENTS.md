# kernel/ — 核心子系统

**85 源文件，5 子目录。** Turn 流水线、多核调度、意图/规划/路由、扩展钩子系统。

## OVERVIEW

Agent 的大脑。消息从 IPC 总线和通道流入，经 intent→plan→turn 流水线处理后通过工具执行和通道返回。

## STRUCTURE

```
kernel/
├── loop.c/runtime.c/sysctl.c        # 主循环 + 运行时配置
├── turn_{prepare,run,exec,finish,persist,dispatch,context,common}.c  # Turn 流水线
├── intent.c/plan.c/roles.c/router.c # 意图/规划/角色/路由
├── hooks.c/state.c/cancel.c         # 扩展钩子 + 取消令牌
├── context_{build,compress,ops}.c   # 上下文构建/压缩
├── channel_{policy,router,runtime}.c # 通道策略/路由/分发
├── tool_{feedback,guard,notify,exec_fail}.c + auto_verify.c  # 工具反馈系统
├── executor_core.c/memory_core.c/compaction.c  # 多核执行器
├── interview.c/learning.c/team.c/ralph.c/todo.c/recovery.c/rules.c  # 功能模块
├── work_item.c/workqueue.c/debug.c/self_test.c  # 工具类
├── sched/                           # 多 Agent 调度器
├── time/                            # Cron 定时器
├── printk/                          # 内核风格日志
├── irq/                             # 信号存根
└── driver/                          # 驱动核心桥接
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Agent 主循环 | `loop.c:agent_loop_task()` | 弹出消息→hook→turn 流水线 |
| Turn 准备 | `turn_prepare.c` | 构建 system prompt，加载历史 |
| Turn 执行 | `turn_run.c` | LLM 工具调用循环，模型回退 |
| 意图分类 | `intent.c` | QA/IMPLEMENT/INVESTIGATE/FIX/OPEN |
| 调度器 | `sched/core.c:sched_dispatch()` | 按 intent 映射 PLANNER/EXECUTOR/REVIEWER |
| 多核 IPC | `executor_core.c` + `memory_core.c` | core_send/recv，3 核（0/1/2） |
| 上下文压缩 | `context_compress.c` | 后台线程，阈值触发 |
| 钩子系统 | `hooks.c` | intent→prepare→before_run→replace_run→finish |
| Plan 评审 | `plan.c` | LLM 生成 plan，注入 system prompt |
| 角色路由 | `roles.c` + `router.c` | intent→role 映射 + 模型选择 |
| Cron 定时 | `time/timer.c` | 周期/单次/每日/每周，SPIFFS 持久化 |

## CONVENTIONS

- **Turn 流水线**：每个阶段独立文件（`turn_*.c`），严格顺序
- **多核架构**：CORE_SCHEDULER(0)/MEMORY(1)/EXECUTOR(2)，通过 `core_task` 通信
- **调度类**：PLANNER(0)→EXECUTOR(1)→REVIEWER(2)，按 intent 映射，最多 4 agent
- **钩子链**：5 个生命周期钩子，扩展通过 `extensions/` 注册
- **取消令牌**：`kernel/cancel.c` 提供协作式取消（非强制）
- **日志**：`pr_info/pr_err/pr_warn/pr_debug`，支持 LOG_HOOK 拦截
- **中文注释**：项目约定，核心接口使用中文 doc（如 `loop.h`："智能体主循环接口"）
- **配置钳制**：运行时 JSON 配置通过 `runtime_config_clamp_int()` 钳制

## ANTI-PATTERNS

- **PLANNER 不可写代码**：`sched/class.c` 调度约束
- **Plan 不可含 TODO/TBD**：`plan.c` 占位符检测 → 重生成
- **不可半成品消息**：回合结束时 `ralph.c` 强制注入 TODO 警告
- **调度器串行执行**：所有 agent 同步运行（`sched_agent_launch()` 是阻塞 LLM 调用）

## KEY STRUCTS

| Struct | File | Role |
|--------|------|------|
| `sched_agent` | `sched/sched.h` | 可调度 agent（pid/class/state/response） |
| `sched_runqueue` | `sched/sched.h` | Agent 运行队列（4 agent max, merged output） |
| `sched_class` | `sched/sched.h` | 调度类（name/priority/prompt_suffix） |
| `core_task` | `include/linux/core_task.h` | 核间任务（id/type/status/payload/result） |
| `agent_extension_hooks_t` | `hooks.h` | 扩展钩子虚表（5 生命周期） |
| `turn_snapshot` | `turn_context.h` | 异步 turn 状态快照 |
| `cron_job_t` | `time/timer.h` | Cron 作业定义 |
