# 代马 Daima Agent 架构总览

## 1. 这是什么

代马（Daima）是一个用 **C11** 编写的嵌入式 AI Agent，运行在 Vector 机器人上，也支持 Linux Host 本地调试。它通过大模型 API 实现多轮对话、工具调用、语音交互和机器人控制。

**一句话**：一个跑在机器人里的 ChatGPT-style Agent，能说话、能听懂中文、能操控硬件。

## 2. 构建与平台

| 目标 | 命令 | 说明 |
|------|------|------|
| Linux Host | `./build.sh` | 本地调试，用系统库 |
| MIPS 交叉编译 | `./build.sh mips` | 生产环境 (Vector 机器人) |
| ARM 交叉编译 | `./build-arm.sh` | ARM 平台 |

- 构建系统：**CMake**（根目录单个 `CMakeLists.txt`，不在子目录分散）
- 依赖：`cJSON`、`libcurl`、`openssl`
- 第三方库集中在 `third_party/`

## 3. 核心架构

所有消息通过 `message_bus` 的两条队列流转：**入站队列**（通道 → Agent）和**出站队列**（Agent → 通道）。

消息结构 `daima_msg_t` 包含 `channel`、`chat_id`、`source`、`content`、`reasoning`、`image_path`、`intent`。

支持五种通道：`feishu`（飞书）、`vector`（机器人）、`websocket`（Web UI）、`voice`（语音）、`pet`（Web 宠物）。

### 3.1 Agent 主循环 (`agent/`)

Agent 循环是一个独立任务，跑在 Core 1，栈大小 24KB。每个 turn 的生命周期：**prepare → run → finish**。

- **prepare** (`agent_turn_prepare.c`)：组装 system prompt + 上下文 + 工具定义
- **run** (`agent_turn_run.c`)：调用 LLM，处理流式响应，解析工具调用
- **finish** (`agent_turn_finish.c`)：持久化对话、触发后续处理

关键子模块：

| 文件 | 职责 |
|------|------|
| `context_builder.c` | 构建 LLM 上下文（历史消息 + skill + 系统提示） |
| `context_compressor.c` | 上下文过长时压缩/摘要 |
| `intent_gate.c` | 意图识别与分类 |
| `category_router.c` | 按消息类别路由到不同模型/参数 |
| `agent_coordinator.c` | 多 Agent 协调（拆分子任务 → 并行执行 → 合并结果） |
| `team_mode.c` | 团队协作模式 |
| `todo_enforcer.c` | 强制 Agent 遵循 TODO 计划 |
| `tool_feedback.c` | 工具调用结果反馈处理 |
| `tool_protocol_guard.c` | 工具调用协议校验 |
| `channel_policy.c` | 通道级策略控制 |
| `plan_review.c` | 计划审查 |
| `learning_review.c` | 学习回顾 |
