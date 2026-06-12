# Tools 子系统

**模块**: `main/tools/`
**职责**: 工具注册、实现、按通道过滤

## STRUCTURE

```
main/tools/
├── tool_registry.c/h           # 工具注册表，按通道过滤
├── tool_invocation_context.c/h # 工具调用上下文
├── tool_runtime.c/h            # 运行时工具状态
├── tool_files.c/h              # 文件操作聚合入口
├── tool_file_*.c/h             # 文件操作子模块（read/list/search/mutations/...）
├── tool_cron.c/h               # 定时任务工具
├── tool_system.c/h             # 系统信息工具
├── tool_terminal_exec.c/h      # 终端执行工具
├── tool_todo.c/h               # 待办事项工具
├── tool_work_item.c/h          # 工作项工具
├── tool_skills.c/h             # Skill 调用工具
├── tool_session_search.c/h     # 会话搜索工具
├── tool_vector_*.c/h           # Vector 机器人控制工具
├── tool_weather_host.c         # 天气查询（Host）
├── tool_get_time_host.c        # 时间查询（Host）
├── tool_webfetch.c             # Web 抓取
└── tool_daima_log.c            # 日志查询
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 注册新工具 | `tool_registry.c` | 在 `tool_registry_init()` 中添加 |
| 工具列表过滤 | `tool_registry.c` | `tool_registry_get_tools_json_for_channel()` |
| 文件操作 | `tool_files.c` | 聚合入口，分发到子模块 |
| Vector 控制 | `tool_vector_*.c` | motion/body/audio/anim/status |
| 会话搜索 | `tool_session_search*.c` | scan + render |

## CONVENTIONS

- 工具定义：`daima_tool_t` {name, description, input_schema_json, execute}
- 通道过滤：PC/WebSocket 不暴露机器人控制工具；Vector/voice 通道暴露
- 文件操作分细粒度子模块，聚合入口在 `tool_files.c`

## ANTI-PATTERNS

- 不要暴露机器人控制工具给非 Vector/voice 通道
- 不要在工具实现中阻塞（异步模型）
- 不要硬编码文件路径（使用 `daima_paths.c`）
