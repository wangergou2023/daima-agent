---
name: Daima 日志诊断
description: 当工具执行失败、出现异常或需要排查问题时，使用 daima_log 读取自身运行日志进行自诊断。
---

# Daima 日志诊断

daima 所有运行日志（INFO/WARN/ERROR/DEBUG）自动写入环形日志文件（最大 512KB），可通过 `daima_log` 工具读取。

## 何时使用

- webfetch / terminal / 任何工具返回错误时，用 `daima_log search` 搜关键词定位根因
- 收到用户反馈"怎么异常了""为什么失败了"时，先查日志再回答
- LLM API 返回 400/500 错误时，查日志中的 API 错误详情
- 构建失败、网络超时等系统级问题排查

## 常用命令

- `daima_log tail` — 最近 50 行（默认）
- `daima_log tail lines=100` — 最近 100 行
- `daima_log search pattern="关键词"` — 大小写不敏感搜索，最多 50 条
- `daima_log errors` — 只看最近 50 条 WARN/ERROR
- `daima_log errors lines=200` — 最近 200 条错误

## 注意事项

- 日志文件路径：`{DAIMA_HOME}/spiffs_data/memory/daima.log`
- 不要用 `read_file` 直接读日志文件（无法按级别和关键词过滤，浪费 token）
- 每条搜索/过滤最多返回 200 行，超过会被截断
- 工具失败时优先用 `search pattern="<工具名>"` 定位相关日志
