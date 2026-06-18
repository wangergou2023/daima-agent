# MEMORY SUBSYSTEM KNOWLEDGE BASE

**Generated:** 2026-06-19
**Commit:** 0d927af
**Branch:** main

## OVERVIEW

会话存储 + 长期记忆管理。`memory_store` 持久化键值对与每日笔记，`session_store` 管理对话历史文件，支持事实提取、上下文压缩摘要、会话恢复。postcore_initcall（level 2），IPC 之后、总线之前初始化。

## STRUCTURE

```
drivers/memory/
├── memory_store.c                # 长期记忆读写（MEMORY.md + 每日 YYYY-MM-DD.md）
├── memory_store.h                # 记忆接口：init/read/write/append_today/read_recent
├── session_store.c               # 会话存储抽象层——ops 模式调度
├── session_store.h               # 抽象接口 + session_store_ops_t + session_record_t
├── session_store_file.c          # 文件型后端：JSONL 读写/重写/遍历/清理
├── session_store_file_internal.h # 内部接口：artifact_path + facts/summary 函数声明
├── session_store_file_common.c   # 通用文件读写：read_all / write_all
├── session_store_file_common.h   # 通用文件读写声明
├── session_store_file_facts.c    # 事实卡片：读取/合并/去重（12 行上限）
├── session_store_file_summary.c  # 摘要读写（上下文压缩）
└── Makefile                      # obj-y := memory_store.o session_store.o ...
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 长期记忆读写 | `memory_store.c` | SPIFFS 扁平文件：`MEMORY.md` + `YYYY-MM-DD.md` |
| 记忆初始化 | `memory_store.c:memory_store_init()` | 确认 base 路径可访问 |
| 会话后端选择 | `session_store.c:ensure_store_ready()` | 动态绑定 `session_store_file_backend()` |
| 消息追加 | `session_store_file.c:file_append_ex()` | JSONL 逐行追加 + `flock(LOCK_EX)` |
| 历史读取 | `session_store_file.c:file_get_history_json()` | 环形缓冲取最近 N 条，过滤压缩摘要 |
| 历史重写 | `session_store_file.c:file_rewrite_from_array()` | cJSON 数组→JSONL 全量覆写 |
| 事实合并 | `session_store_file_facts.c:session_store_file_merge_facts()` | 去重追加，12 行上限，FIFO 淘汰 |
| 压缩摘要 | `session_store_file_summary.c:session_store_file_write_summary()` | 带时间戳的 Markdown 摘要 |
| 会话列举 | `session_store_file.c:file_list_records()` | 扫描目录，合并 jsonl/facts/summary 为 session_record_t |
| 文件读写通用 | `session_store_file_common.c` | `session_file_read_all` / `session_file_write_all` |
| 文件路径生成 | `session_store_file_internal.h:session_store_file_artifact_path()` | `session_{chat_id}.jsonl` / `_facts.md` / `_summary.md` |

## CONVENTIONS

- **SPIFFS 扁平后端：** `memory_store` 直接读写 `spiffs_data/` 下文件，无目录层级
- **JSONL 格式：** 会话历史每行一个 JSON 对象 `{role, content, source?, ts}`，文件级 `flock` 保护
- **ops 模式：** `session_store` 通过 `session_store_ops_t` 函数表解耦，当前唯一实现为 `session_store_file_backend`
- **事实去重：** 合并时按行去重（忽略 `-` / `*` / `+` 前缀），最多 12 行，超出时 FIFO 淘汰旧行
- **压缩摘要过滤：** `[上下文压缩摘要]` 前缀的消息不在历史读取中返回
- **环形缓冲读取：** `file_get_history_json` 用 `SESSION_MAX_MSGS` 环形数组取尾部 N 条
- **初始化链：** `memory_store_init()` 在 `postcore_initcall(2)` 调用，`session_store_init()` 首次使用时延迟绑定
