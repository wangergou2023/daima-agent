# PROJECT KNOWLEDGE BASE

**Generated:** 2026-06-22
**Branch:** main

## OVERVIEW

Daima Agent 是一个 `C11 + Kbuild` 的单二进制 AI Agent。默认框架只认 `kernel` 主链，`subagent` 只走 `delegate_task + kernel/sched`。

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 启动流程 | `init/main.c` → `init/bootstrap.c` | 4 级手动初始化链 |
| 默认回合主链 | `kernel/loop.c` | intent / role / plan / model route |
| turn 准备 | `kernel/turn_prepare.c` | prompt / history |
| turn 执行 | `kernel/turn_pipeline.c` | interview → run → finalize |
| turn 收尾 | `kernel/turn_finish.c` | 回复、持久化、Ralph、回收 |
| subagent 调度 | `kernel/sched/core.c` | `PLANNER / EXECUTOR / REVIEWER` |
| subagent 工具入口 | `drivers/tool/tool_delegate.c` | `delegate_task` |
| skill 摘要 | `drivers/skill/skill_summary.c` | 只构建摘要 |
| skill 工具激活 | `drivers/skill/skill_tools.c` | 显式注册 / 注销 |

## CURRENT FLOW

`do_basic_setup()`：

- `core`：message bus、IPC、extensions
- `postcore`：memory / session store
- `subsys`：cron / heartbeat / proxy / skill loader
- `device`：三条总线、executor core、memory core

`process_new_message()`：

1. 处理内部控制消息与 `!test`
2. 重置回合状态
3. 意图分类
4. 角色选择
5. 计划生成
6. `agent_turn_prepare()`
7. role prompt + team guidance
8. model route
9. `agent_run_prepared_turn()`

`agent_run_prepared_turn()`：

1. `agent_turn_maybe_interview()`
2. `agent_turn_run()`
3. `agent_finalize_turn()`

## RULES

- 默认主链只在 `kernel/`
- `subagent` 只允许走 `delegate_task + kernel/sched`
- skill 摘要不等于 skill 工具已激活
- `!test` 是内建自检命令，不对应 `test/` 目录
