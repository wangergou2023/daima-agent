---
name: 每日简报
description: 当用户要求每日简报、早报、今日安排、提醒汇总或定时推送今日信息时使用。
---

# 每日简报

生成简洁、实用的每日信息汇总，适合作为早报、今日提醒或 heartbeat/cron 推送内容。

## 何时使用

- 用户要求每日简报、早报、今天安排、今日提醒。
- heartbeat 或 cron 任务需要生成当天摘要。
- 用户询问项目进展、周报或今日工作概况。

## 使用步骤

1. 用 `get_current_time` 获取当前日期。
2. 读取 `/spiffs/memory/MEMORY.md` 获取用户偏好与稳定上下文。
3. 读取今日笔记；不存在则跳过。
4. 若用户资料或上下文中有地点，用 `weather` 获取天气。
5. 调用 `cron action=list` 获取定时任务。
6. 如果涉及项目进展，读取 `/spiffs/memory/PROGRESS.md` 并按 `progress-tracker` 口径整理。
7. 汇总成 5-10 条以内的简报。

## 工具与路径

- 常用工具：`get_current_time`、`files action=read`、`weather`、`cron action=list`。
- 常用路径：`/spiffs/memory/MEMORY.md`、`/spiffs/memory/PROGRESS.md`。

## 输出要求

- 包含日期、天气、待办、提醒、项目进展或 deadline 中相关项。
- 保持简短，使用用户偏好语言。

## 注意事项

- 天气返回较长时压缩成 1-3 行重点。
- 没有今日笔记或定时任务时直接跳过，不要硬编。
- 用户要更简短版本时，优先保留日期、天气、待办和提醒。
