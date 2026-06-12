# App 子系统

**模块**: `main/app/`
**职责**: 启动、运行时配置、路径解析、文件系统、通道路由

## STRUCTURE

```
main/app/
├── daima_bootstrap.c/h         # 启动准备：目录布局、配置加载
├── runtime_config.c/h          # JSON 配置解析与访问器
├── runtime_config_sections.c/h # 配置分段加载
├── daima_paths.c/h             # 运行时路径解析
├── daima_fs.c/h                # 文件系统辅助
├── channel_router.c/h          # 消息通道路由
├── channel_runtime.c/h         # 通道运行时状态
├── tool_activity_notifier.c/h  # 工具活动通知
└── interactive_requests.c/h    # 交互式请求处理
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 启动准备 | `daima_bootstrap.c` | 创建 SPIFFS 风格目录布局 |
| 配置加载 | `runtime_config.c` | 解析 config.json，覆盖编译期默认值 |
| 路径解析 | `daima_paths.c` | `DAIMA_HOME` → 可执行文件位置 → `~/.daima` |
| 通道路由 | `channel_router.c` | 消息分发到对应通道 |

## CONVENTIONS

- 运行时路径优先级：`DAIMA_HOME` > 可执行文件位置 > `~/.daima`
- 配置分层：编译期宏（`daima_config.h`）→ 运行时 JSON（`config.json`）
- 路径函数统一以 `daima_path_` 为前缀

## ANTI-PATTERNS

- 不要硬编码路径（使用 `daima_paths.c`）
- 不要绕过 `runtime_config` 直接读取环境变量
- 不要在 bootstrap 中初始化业务模块（只准备环境）
