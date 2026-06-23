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
| 主循环 | `loop.c` | 事件循环与消息派发 |
| 单回合入口 | `turn_entry.c` | 新消息前置判断与主链编排 |
| 回合前置 | `turn_gate.c` | `!test` 与 internal control 过滤 |
| 回合决策 | `turn_decision.c` | intent / role / plan / model |
| prompt 注入 | `turn_prompt.c` | role prompt / team guidance |
| 回合临时 I/O | `turn_io.c` | 当前同步回合的 prompt/history/messages |
| turn 准备 | `turn_prepare.c` | prepare orchestrator |
| prompt 注入链 | `turn_prompt_build.c` | rules / summary / facts / recovery / todo / runtime context / plan |
| message 组装 | `turn_message_build.c` | history JSON + current turn messages + vision content |
| turn 执行 | `turn_pipeline.c` | prepared turn execute → finalize |
| interview 短路 | `turn_interview.c` | clarification/interview short-circuit |
| turn 收尾 | `turn_finish.c` | finish orchestrator |
| reply 处理 | `turn_reply.c` | cancelled / reply / save / queue / Ralph |
| 收尾副作用 | `turn_post.c` | cleanup / recovery / todo / compaction / tool cleanup |
| subagent 调度 | `sched/core.c` | `sched_dispatch()` |
| 模型路由 | `router.c` | intent / role → model |
| 异步恢复快照 | `turn_context.c` | resume 所需 snapshot store |

## DEFAULT CHAIN

默认执行链：

1. `loop.c`
2. `turn_entry.c`
3. `turn_prepare.c`
4. `turn_pipeline.c`
5. `turn_finish.c`

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
