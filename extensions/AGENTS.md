# extensions/ — 固定流水线阶段模块

**10 文件，8 个固定阶段模块。** 通过 `extensions_init()` 在 `bootstrap.c` 中显式调用，扩展 Agent 核心行为。

## OVERVIEW

`#if AGENT_EXTENSIONS_ENABLED` 条件编译。每个模块实现 `agent_extension_hooks_t` 钩子集（on_intent / on_prepare / before_run / replace_run / on_finish），在 Turn 流水线不同阶段介入。

这里的 `extensions/` 更接近“固定顺序的阶段模块集合”，不是运行时热插拔插件系统：
- 模块集合固定
- 初始化顺序固定
- 主要价值在于把阶段逻辑分文件组织，而不是提供真实动态扩展能力

## STRUCTURE

```
extensions/
├── module_intent.c       # 意图分类（on_intent → intent_gate_classify）
├── module_roles.c        # 角色链分配（on_intent + on_prepare）
├── module_plan.c         # 计划生成评审（on_intent + on_prepare）
├── module_router.c       # 分类模型路由（before_run → model_override）
├── module_interview.c    # Prometheus 访谈（replace_run，信息不足时提问）
├── module_sched.c        # 多 Agent 调度（replace_run → PLANNER+EXECUTOR+REVIEWER）
├── module_team.c         # 团队模式编排（replace_run，多子 Agent 并行）
├── module_ralph.c        # Ralph Loop 续推（on_prepare + on_finish → TODO 未完成警告）
├── ext_ralph_loop.h      # Ralph Loop 公开接口
├── ext_init.h            # 8 模块 init 函数声明 + extensions_init() 原型
├── ext_init.c            # extensions_init()：按序调用 8 个 __init 函数
└── Makefile              # obj-y 列表（8 .o）
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 模块注册 | `ext_init.c:extensions_init()` | 按序显式调用 8 模块 `__init` 函数，`bootstrap.c` 中调用 |
| Ralph Loop | `module_ralph.c:46` | `agent_extension_ralph_should_append_warning()` |
| 计划验证 | `module_plan.c:16` | `plan_review_generate()` 拒绝 TODO/TBD 占位符 |
| 意图门 | `module_intent.c:21` | `intent_gate_classify()` 5 类（QA/IMPLEMENT/INVESTIGATE/FIX/OPEN）|
| 访谈 | `module_interview.c:19` | `replace_run` 钩子，单角色 IMPLEMENT 信息不足时提问 |
| 调度 | `module_sched.c:31` | `sched_dispatch → start → wait → merge` 四阶段 |
| 团队 | `module_team.c:24` | `replace_run` 钩子，有计划时启动多子 Agent |

## CONVENTIONS

- `extensions_init()` 在 `init/bootstrap.c:do_basic_setup()` 中按顺序调用 8 模块：intent→interview→plan→ralph→roles→router→sched→team
- `module_init()` / `device_initcall()` 现为空宏（initcall section 机制已移除）
- 所有逻辑包裹 `#if AGENT_EXTENSIONS_ENABLED`，未启用时直接返回 0
- `replace_run` 失败返回 `ERR_FAIL` 让钩子链继续（退化为默认 LLM 调用）
- 钩子注册：填充 `agent_extension_hooks_t`，调用 `agent_hooks_register(&ext)`

## ANTI-PATTERNS

- **Plan 不可含 TODO/TBD** — `plan_review_generate()` 拒绝并重生成（`module_plan.c:16`）
- **PLANNER 不可写代码** — `SCHED_CLASS_PLANNER` prompt 强制 `Do NOT write code`（`kernel/sched/class.c:14`）
- **不可发半成品消息** — Ralph Loop 在回合结束时有未完成 TODO 强制追加续推警告（`module_ralph.c:46`）
- **子 agent 不可递归委托** — 见 `drivers/tool/tool_delegate.c`，团队模式不产生嵌套
