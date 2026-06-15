---
name: Agent 日志诊断
description: 当用户遇到工具执行失败、系统异常、LLM API 报错或追问失败原因时使用。
---

# Agent 日志诊断

Agent 运行日志会写入环形日志文件，可通过 `log_tool` 工具按 tail、search、errors 等方式读取。

## 何时使用

- `webfetch`、`terminal` 或任何工具返回错误。
- 用户反馈“怎么异常了”“为什么失败了”。
- LLM API 返回 400/500 或网络超时。
- 构建失败、系统级异常、需要自诊断。

## 使用步骤

1. 先用 `log_tool errors` 查看最近 WARN/ERROR。
2. 根据工具名、请求 ID、错误关键字用 `log_tool search pattern="关键词"` 定位上下文。
3. 如果需要时序信息，用 `log_tool tail lines=100`。
4. 结合用户看到的错误和日志证据判断根因。
5. 回复时说明日志证据、可能原因和下一步处理。

## 工具与路径

- 常用命令：`log_tool tail`、`log_tool search pattern="关键词"`、`log_tool errors`。
- 日志路径：`/spiffs/memory/agent.log`。

## 输出要求

- 不要整段贴日志；摘出关键错误和上下文。
- 明确区分“日志证明的事实”和“基于事实的推断”。

## 注意事项

- 不要直接用 `files action=read` 读取日志文件，优先使用 `log_tool` 过滤。
- 工具失败时优先搜索工具名。
- 每次搜索/过滤可能截断，必要时换关键词缩小范围。
