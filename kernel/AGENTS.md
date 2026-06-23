# kernel/ — 核心子系统

`kernel/` 是默认框架的唯一主链。

## OVERVIEW

这里负责：

- turn 流水线
- 多核执行
- 意图 / 角色 / 计划 / 路由
- subagent 调度
- Ralph / interview / todo / recovery

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 主循环 | `loop.c` | 新消息入口与主链状态装配 |
| turn 准备 | `turn_prepare.c` | prompt / history / plan |
| turn 执行 | `turn_pipeline.c` | interview → run → finalize |
| turn 收尾 | `turn_finish.c` | 回复、持久化、Ralph、回收 |
| subagent 调度 | `sched/core.c` | `sched_dispatch()` |
| 模型路由 | `router.c` | intent / role → model |
| 回合状态 | `state.c` | plan / roles / active_role |

## DEFAULT CHAIN

默认执行链：

1. `loop.c`
2. `turn_prepare.c`
3. `turn_pipeline.c`
4. `turn_finish.c`

默认构建下：

- 不依赖仓库外层扩展目录
- 不依赖 hook 改写主流程
- 不通过旁路替换执行链

## SUBAGENT

`subagent` 属于主链能力，不属于扩展：

- 调度在 `sched/`
- 入口在 `drivers/tool/tool_delegate.c`
- 复杂任务可以并行拆分
- 不允许递归委托
