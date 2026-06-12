# Agent 子系统

**模块**: `main/agent/`
**职责**: Agent 主循环、turn 生命周期管理、上下文构建与压缩

## STRUCTURE

```
main/agent/
├── agent_loop.c/h              # 主循环：消息入队 → LLM → 工具调用 → 响应
├── agent_turn_*.c/h            # Turn 生命周期（prepare/run/exec/finish/persist）
├── context_builder.c/h         # 上下文构建（历史 + skill + system prompt）
├── context_compressor.c/h      # 上下文压缩策略
├── context_compress_ops.c/h    # 压缩操作实现
├── tool_feedback.c/h           # 工具调用结果反馈
├── tool_protocol_guard.c/h     # 工具协议校验
├── agent_cancel.c/h            # 取消机制
├── channel_policy.c/h          # 通道策略
├── learning_review.c/h         # 学习回顾
└── agent_prompt_debug.c/h      # Prompt 调试输出
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 启动 Agent | `agent_loop.c` | `agent_loop_init()` → `agent_loop_start()` |
| Turn 执行 | `agent_turn_run.c` | 核心 turn 执行逻辑 |
| 上下文构建 | `context_builder.c` | 组装 messages 数组 |
| 上下文压缩 | `context_compressor.c` + `context_compress_ops.c` | 超长时压缩中间消息 |
| 工具反馈 | `tool_feedback.c` | 将工具结果加入上下文 |
| 协议校验 | `tool_protocol_guard.c` | 校验工具调用格式 |
| 取消处理 | `agent_cancel.c` | 用户取消/超时处理 |

## CONVENTIONS

- Turn 分阶段：prepare → run → exec helpers → finish → persist
- 上下文压缩在 `DAIMA_CONTEXT_COMPRESS_ENABLED` 开启时自动触发
- Prompt 调试快照输出到 `~/.daima/last_prompt.md`
- 通道策略决定不同通道的 system prompt 差异

## ANTI-PATTERNS

- 不要在 agent loop 中阻塞（使用 queue + task 模型）
- 不要绕过 `tool_protocol_guard` 直接执行工具调用
- 不要修改 turn 状态机顺序
