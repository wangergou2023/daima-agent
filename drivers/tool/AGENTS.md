# drivers/tool/ — 工具驱动系统

**60 源文件。** Agent 可调用的 34 工具能力，注册在 `tool_bus` 上。

## OVERVIEW

工具是 Agent 与外部世界交互的唯一途径。每个工具是一个 `struct tool_driver`，嵌入 `struct driver` 作为首字段，注册在 `tool_bus` 上按名称匹配绑定。

## STRUCTURE

```
drivers/tool/
├── tool_dynamic_bus.c/h      # 动态工具注册到 tool_bus
├── tool_types.h              # struct tool / tool_driver / tool_device
├── tool_fs.c                 # 文件操作（读写/列表/创建/删除）
├── tool_terminal.c           # 终端命令执行
├── tool_webfetch.c           # Web 抓取
├── tool_cron.c               # Cron 工具
├── tool_todo.c               # Todo 管理
├── tool_skills.c             # 技能调用
├── tool_system.c             # 系统信息
├── tool_file_mutations.c     # 文件编辑（hashline 安全编辑）
├── tool_delegate.c           # 子 Agent 委托
├── tool_safe_edit.c          # 安全文件编辑
├── tool_work_item.c          # 工作项管理
├── tool_custom.c             # 自定义工具（JSON 驱动）
├── tool_weather_host.c       # 天气查询（示例工具）
├── tool_{vector,tts,gpio}*.c # Vector 机器人/语音/GPIO
├── tool_session_*.c          # 会话工具
└── Makefile                  # obj-y 列表
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 注册动态工具 | `tool_dynamic_bus.c` | driver_register + device_register 配对 |
| 工具驱动模式 | `tool_types.h` | struct tool_driver 定义 |
| 自定义工具 | `tool_custom.c` | JSON 驱动，`custom_tools.json` |
| 文件编辑 | `tool_file_mutations.c` + `tool_safe_edit.c` | Hashline 安全编辑 |
| Web 抓取 | `tool_webfetch.c` | libcurl HTTP |
| 子 Agent | `tool_delegate.c` | 禁止递归委托 |
| 天气（示例） | `tool_weather_host.c` | 规范工具模式参考 |

## CONVENTIONS

### 工具驱动三部曲

```c
// 1. 工具设备（总线层）
struct tool_device { name, description, input_schema_json };

// 2. 工具驱动（总线层） — struct driver 必须是首字段！
struct tool_driver {
    struct driver drv;      // 首字段 → container_of
    err_t (*execute)(const char *input, char *output, size_t size);
};
```

### 注册流程

内置工具由 `tool_builtin_bus_init()` 注册；动态工具由 `tool_dynamic_bus_register()` 配对 `driver_register()` + `device_register()` 到 `tool_bus`。

### 自定义工具

`tool_custom.c` 读取 `spiffs_data/config/custom_tools.json`：
```json
{"name": "my_tool", "driver": "terminal_exec", "command": "my_tool "}
```
`driver` 匹配现有 tool_driver，`command` 前缀追加到输入。

### 工具执行与验证

- `tool_bus_execute(name, ...)` 查找并执行
- 工具失败通过 `kernel/tooling/tool_exec_fail.c` 观察
- 文件编辑工具通过 `kernel/auto_verify.c` 自动验证副作用

## ANTI-PATTERNS

- **子 Agent 不可递归**：`tool_delegate.c` 禁止 `delegate_task()` 调用
- **工具不可修改全局状态**：工具应为纯函数（文件编辑是特例）
- **不可绕过总线注册**：所有工具必须通过 `tool_builtin_bus_init()`、`tool_dynamic_bus_register()` 或 `tool_custom_load_default()`
- **struct driver 必须是 tool_driver 首字段**：否则 `container_of` 会返回错误指针
