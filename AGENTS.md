# daima-agent Knowledge Base

**Generated:** 2026-06-12
**Commit:** 2b63329
**Branch:** main

## OVERVIEW

代马 AI 助手 — Vector 机器人嵌入式 Agent。C11 编写的 FreeRTOS-style 多任务系统，支持中文语音对话、AI Agent 推理、机器人控制。可运行于 Linux Host（调试）或 MIPS/ARM 嵌入式平台（生产）。

## STRUCTURE

```
.
├── main/               # C 源码（按子系统组织）
│   ├── app/            # 启动、运行时配置、路径、文件系统
│   ├── agent/          # Agent 主循环、turn 生命周期、上下文管理
│   ├── tools/          # 工具注册与实现（文件、cron、机器人控制等）
│   ├── channels/       # 消息通道（飞书、Vector 机器人）
│   ├── llm/            # 大模型代理、OpenAI/Anthropic 协议适配
│   ├── gateway/        # WebSocket/HTTP 网关（Web UI）
│   ├── memory/         # 会话持久化、存储
│   ├── voice/          # ASR/TTS、语音通道
│   ├── vision/         # 图像抓拍（MIPS 平台）
│   ├── audio/          # 音频 IO（MIPS 平台）
│   ├── bus/            # 消息总线
│   ├── cron/           # 定时任务
│   ├── skills/         # Skill 加载器（C 侧）
│   └── platform/       # OS 抽象层
├── spiffs_data/        # 运行时数据（SPIFFS 风格目录布局）
│   ├── skills/         # LLM Skill 定义（Python + SKILL.md）
│   ├── config/         # 运行时配置（config.json）
│   └── web/            # Web UI 静态资源
├── test/               # C 单元测试（Makefile 驱动）
├── third_party/        # 依赖（curl, cJSON）
├── docs/               # 项目文档
└── build-*/            # 构建输出
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 启动流程 | `main/daima_host.c` | `main()` → bootstrap → init all → loop |
| 运行时配置 | `main/app/runtime_config.c` | JSON 配置解析，覆盖编译期默认值 |
| Agent 循环 | `main/agent/agent_loop.c` | 消息入队 → LLM → 工具调用 → 响应 |
| 工具注册 | `main/tools/tool_registry.c` | 按通道过滤工具列表 |
| 飞书通道 | `main/channels/feishu/` | WS 连接、事件处理、媒体上传 |
| Vector 通道 | `main/channels/vector/` | MCP 客户端、音频缓冲、机器人控制 |
| LLM 适配 | `main/llm/llm_proxy_host.c` | 流式请求、分片解析 |
| Skill 系统 | `main/skills/skill_loader.c` + `spiffs_data/skills/` | C 加载器 + Python 脚本 |
| 构建脚本 | `CMakeLists.txt` + `build.sh` | Host/MIPS/ARM 三平台 |

## CONVENTIONS

- **C11**，`#pragma once` 头文件保护
- **命名**：模块前缀 + 下划线（`daima_*`, `agent_*`, `tool_*`）
- **错误码**：`daima_err_t` 枚举，`DAIMA_ERROR_CHECK(x)` 宏（失败 abort）
- **日志**：`DAIMA_LOGI(TAG, fmt, ...)` 四级日志
- **OS 抽象**：`daima_os.h` 提供 queue/event/task/timer，Host 用 pthread 实现
- **平台切换**：Host stub（`_stub.c`）vs MIPS 实现（`_mips.c`），由根 CMake 控制
- **SPIFFS 布局**：运行时自动创建 `~/.daima/` 下的 config/memory/sessions/cache/web/skills/workspace

## ANTI-PATTERNS

- 不要在 `main/` 下新增 CMakeLists.txt（构建集中在根 CMakeLists.txt）
- 不要硬编码路径（使用 `daima_paths.c` 解析的 runtime 路径）
- 不要在 agent loop 中阻塞（使用 queue + task 模型）
- 不要修改 `third_party/`（升级通过替换版本）
- 不要暴露机器人控制工具给非 Vector/voice 通道（`tool_registry_get_tools_json_for_channel` 过滤）

## COMMANDS

```bash
# Host 本地编译
./build.sh

# MIPS 交叉编译
./build.sh mips

# ARM 交叉编译（vicos-sdk）
./build-arm.sh

# 运行测试
cd test && make && make test

# 部署到机器人
./install-robot.sh <robot-ip>

# 清理构建
./build.sh clean
```

## NOTES

- `config.json` 包含敏感信息（API keys），不提交到 git
- 根 `CMakeLists.txt` 是模块索引：新增源码必须在那里注册
- 大模型固定 OpenAI 兼容协议，默认 DeepSeek v4-pro
- Vision 功能编译开关 `DAIMA_ENABLE_VISION`，Host 为空实现
- 语音唤醒仅在 MIPS 平台真实生效，Host 为 stub
