# Daima Agent 软件说明书

## 1. 文档说明

本文档服务于本项目的持续开发与维护。
本项目的目标是开发一个好用、可维护、可持续演进的 Agent，因此需要一份能够详细说明功能逻辑、执行链路和关键设计约束的软件说明书。

本文档覆盖以下内容：

- 软件定位与组成
- 仓库结构与核心模块职责
- 端到端执行链路
- 配置体系与运行时目录
- 部署、启动、排障与维护要点

本文档描述对象包括两部分：

- 仓库项目：`/home/wangergou/code/github/daima-agent`
- 本机运行目录：`/home/wangergou/.agent-data/spiffs_data`

配套阅读材料：

- 架构框架文档：[ARCHITECTURE.md](/home/wangergou/code/github/daima-agent/docs/ARCHITECTURE.md)
- 项目入口说明：[README.md](/home/wangergou/code/github/daima-agent/README.md)

## 2. 软件概述

Daima Agent 是一个基于 `C11 + Kbuild` 构建的单二进制 AI Agent 系统。系统采用 Linux 内核风格的分层结构，将启动引导、主链编排、能力层、消息总线、运行时配置和会话数据分离管理。

软件的核心目标是提供一条清晰、可读、可维护的默认主链，使用户输入能够经过意图分类、角色决策、提示词与消息准备、LLM 执行、工具调用、结果收尾和通道回传，最终返回给客户端。

这份说明书的作用不是做泛化介绍，而是把系统功能逻辑讲清楚，帮助开发者理解：

- 这个 Agent 的目标能力是什么
- 现有功能是如何串起来工作的
- 关键逻辑分布在哪些模块
- 修改某类能力时应该从哪里入手
- 运行时问题应该如何定位

软件包含以下关键能力：

- 多通道消息接入与回传
- 单回合 Agent 主链编排
- 意图分类、角色调度、模型路由
- LLM 对话与工具调用循环
- 可选 subagent 协作
- 会话、摘要、事实、恢复与压缩
- 技能摘要加载与技能工具激活

## 3. 系统组成

从开发视角看，这个系统不是一组松散功能的堆叠，而是一条围绕 Agent 可用性展开的执行链。系统设计重点是把“输入、决策、执行、协作、返回”拆成清晰模块，便于继续演进功能而不破坏主链。

系统由以下几层组成：

1. 启动层
   - 负责路径准备、目录布局、配置加载、初始化链执行与主服务拉起
   - 主要目录：`init/`

2. 主链层
   - 负责默认 turn 主链
   - 包括消息处理入口、决策、准备、执行、收尾、恢复
   - 主要目录：`kernel/`

3. 能力层
   - 负责工具、LLM、通道、存储、技能等能力实现
   - 主要目录：`drivers/`

4. 通信层
   - 负责 inbound/outbound bus、IPC 与异步执行核通信
   - 主要目录：`ipc/`

5. 基础设施层
   - 负责路径、文件系统、网络、通用库等基础能力
   - 主要目录：`fs/`、`net/`、`lib/`

6. 运行时数据层
   - 负责配置、技能、会话、日志、缓存、Web 资源等运行时数据
   - 主要目录：`spiffs_data/`

## 4. 仓库结构说明

项目仓库主目录如下：

```text
daima-agent/
├── init/            启动引导与初始化链
├── kernel/          默认主链、调度、恢复、收尾
├── drivers/         tool / llm / channel / memory / skill
├── ipc/             message bus、IPC、core_task
├── fs/ net/ lib/    基础设施
├── arch/host/       Host 平台实现
├── scripts/         Kbuild 辅助脚本与开发调试脚本
├── spiffs_data/     配置模板、技能、Web 资源、证书
├── docs/            文档
├── install.sh       安装脚本
└── run.sh           本地启动入口
```

核心阅读入口如下：

| 内容 | 文件 |
|------|------|
| 进程入口 | `init/main.c` |
| 初始化链 | `init/bootstrap.c` |
| 外层循环 | `kernel/loop.c` |
| 单回合入口 | `kernel/turn/turn_entry.c` |
| 回合决策 | `kernel/turn/turn_decision.c` |
| 准备阶段 | `kernel/turn/turn_prepare.c` |
| 执行阶段 | `kernel/turn/turn_pipeline.c` |
| 收尾阶段 | `kernel/turn/turn_finish.c` |
| subagent 调度 | `kernel/sched/core.c` |
| 模型路由 | `kernel/router.c` |
| LLM 代理 | `drivers/llm/llm_proxy.h`、`arch/host/llm_proxy_host.c` |
| 工具运行时 | `drivers/tool/tool_runtime.c` |
| 安装脚本 | `install.sh` |

## 5. 启动与初始化机制

### 5.1 进程入口

进程入口位于：

- [init/main.c](/home/wangergou/code/github/daima-agent/init/main.c)

主流程包括：

1. 准备运行目录
2. 初始化 `spiffs_data` 目录结构
3. 加载运行时配置
4. 执行 `do_basic_setup()`
5. 初始化 LLM 代理、工具总线、主循环与通道服务

### 5.2 运行时目录布局

启动引导代码会确保以下目录存在：

- `config/`
- `memory/`
- `sessions/`
- `cache/`
- `web/`
- `skills/`
- `workspace/`

这部分逻辑位于：

- [init/bootstrap.c](/home/wangergou/code/github/daima-agent/init/bootstrap.c)

### 5.3 四级初始化链

`do_basic_setup()` 固定按四级执行：

| 级别 | 主要内容 |
|------|----------|
| `core` | `message_bus_init()`、`core_ipc_init()` |
| `postcore` | `memory_store_init()`、`session_store_init()` |
| `subsys` | `cron_service_init()`、`heartbeat_init()`、`http_proxy_init()`、`skill_loader_init()` |
| `device` | `bus_init()`、`bus_channel_register_all()`、`bus_llm_register_all()`、`executor_core_start()`、`memory_core_start()` |

该初始化链定义了系统依赖顺序，是部署和排障时的重要依据。

## 6. 核心模块职责

### 6.1 `init/`

负责进程级启动、路径准备、目录初始化和基础服务拉起。

### 6.2 `kernel/`

负责默认主链和核心决策逻辑，包括：

- 输入处理入口
- intent / role / plan / model 决策
- prompt / message 构造
- LLM 执行与工具循环
- 回复收尾与副作用处理
- async resume
- subagent 调度

### 6.3 `drivers/tool/`

负责工具定义和工具运行时，包含 terminal、文件、todo、delegate、webfetch 等工具实现。

### 6.4 `drivers/llm/`

负责统一的 LLM 调用接口、协议适配、payload 构建与模型 fallback。

### 6.5 `drivers/channel/`

负责消息接入与结果回传，支持 websocket、feishu、vector 等通道。

### 6.6 `drivers/memory/`

负责会话记录、摘要、事实、文件型会话存储与相关持久化逻辑。

### 6.7 `drivers/skill/`

负责技能摘要构建、技能元数据、技能工具激活与注销。

### 6.8 `ipc/`

负责 inbound/outbound 消息总线和核间通信。

## 7. 端到端处理流程

这一章是本文档的核心部分。对于继续开发这个项目的人来说，最重要的不是知道文件名本身，而是知道一个用户请求如何在系统中流动，以及每个功能逻辑插在链路的哪个位置。

### 7.1 总体流程

完整主链如下：

1. 用户通过通道发送消息
2. 通道层将消息封装为 `struct message`
3. 消息进入 inbound bus
4. `kernel/loop.c` 取出消息
5. `agent_turn_process_new_message(...)` 启动单回合主链
6. 系统完成决策、准备、执行、收尾
7. 结果进入 outbound bus
8. 通道路由将结果发送回客户端

### 7.2 单回合入口

单回合入口位于：

- [kernel/turn/turn_entry.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_entry.c)

入口函数：

- `agent_turn_process_new_message(struct message *msg)`

执行顺序如下：

1. 处理内部控制消息与 `!test`
2. 校验 inbound message
3. 初始化本轮临时 I/O
4. 重置 turn 决策对象
5. 进行 intent / role / plan 决策
6. 调用 `agent_turn_prepare(...)`
7. 注入 role prompt 与 team guidance
8. 解析工具集合与模型路由
9. 调用 `agent_run_prepared_turn(...)`

### 7.3 决策机制

决策逻辑位于：

- [kernel/turn/turn_decision.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_decision.c)

决策包含四个关键概念：

- `intent`
  - 负责任务分类，如 `qa / implement / investigate / fix / open`
- `role`
  - 负责确定执行角色，如 `FAST / PLANNER / EXECUTOR / REVIEWER`
- `role_model_map`
  - 负责把角色映射到 provider/profile
- `active_provider`
  - 负责角色路由未命中时的兜底 provider

规则如下：

- `intent` 用于任务分类
- `role` 用于调度执行角色
- 主 agent 按 `role_model_map[active_role]` 选模型
- subagent 按自己的角色从 `role_model_map` 选模型
- 角色未命中路由时回退到 `active_provider`

### 7.4 准备阶段

准备阶段位于：

- [kernel/turn/turn_prepare.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_prepare.c)
- [kernel/turn/turn_prompt_build.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_prompt_build.c)
- [kernel/turn/turn_message_build.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_message_build.c)

其中：

- `turn_prepare.c`
  - 负责 prepare orchestrator
- `turn_prompt_build.c`
  - 负责 system prompt 注入链
- `turn_message_build.c`
  - 负责 history JSON 与本轮 messages 组装

准备阶段输出三份关键数据：

- `system_prompt`
- `history_json`
- `messages`

### 7.5 执行阶段

执行阶段位于：

- [kernel/turn/turn_pipeline.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_pipeline.c)
- [kernel/turn/turn_interview.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_interview.c)
- [kernel/turn/turn_run.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_run.c)
- [kernel/turn/turn_exec.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_exec.c)

执行顺序如下：

1. `agent_turn_try_interview(...)`
2. 若命中 interview，直接返回澄清结果
3. 否则进入 `agent_turn_run(...)`
4. 在执行结束后统一 finalize

LLM 工具循环如下：

1. 调用 LLM
2. 判断是否直接得到 final text
3. 若返回 tool calls，则执行工具
4. 将 assistant content 与 `tool_result` 写回 `messages`
5. 继续下一轮 LLM，直到得到 final text

### 7.6 subagent 协作

subagent 不是固定主链阶段，而是执行阶段中的可选分支。

入口与调度位于：

- [drivers/tool/tool_delegate.c](/home/wangergou/code/github/daima-agent/drivers/tool/tool_delegate.c)
- [kernel/sched/core.c](/home/wangergou/code/github/daima-agent/kernel/sched/core.c)
- [kernel/sched/agent.c](/home/wangergou/code/github/daima-agent/kernel/sched/agent.c)

协作机制如下：

1. 主 agent 在工具执行阶段暴露 `delegate_task`
2. 模型显式调用 `delegate_task` 时进入 subagent 调度
3. 调度器生成 `PLANNER / EXECUTOR / REVIEWER` 子角色
4. 每个 subagent 按自身角色选择模型
5. 协作结果回写到当前 turn 的工具结果链中

### 7.7 收尾阶段

收尾阶段位于：

- [kernel/turn/turn_finish.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_finish.c)
- [kernel/turn/turn_reply.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_reply.c)
- [kernel/turn/turn_post.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_post.c)
- [kernel/turn/turn_persist.c](/home/wangergou/code/github/daima-agent/kernel/turn/turn_persist.c)

职责拆分如下：

- `turn_reply.c`
  - cancelled / success / error 回复路径
  - Ralph warning
  - session save
  - outbound queue
- `turn_post.c`
  - cleanup
  - skill tool unregister
  - todo progress record
  - recovery clear
  - context compaction dispatch

## 8. 配置体系说明

### 8.1 配置文件位置

仓库模板配置位于：

- [spiffs_data/config/config.json](/home/wangergou/code/github/daima-agent/spiffs_data/config/config.json)
- [spiffs_data/config/config.example.json](/home/wangergou/code/github/daima-agent/spiffs_data/config/config.example.json)

本机实际运行配置位于：

- `/home/wangergou/.agent-data/spiffs_data/config/config.json`
- `/home/wangergou/.agent-data/spiffs_data/config/config.example.json`

### 8.2 主要配置段

1. `common`
   - 通用上下文、会话、压缩、Web 端口、定时配置

2. `web`
   - Web 相关配置，如默认 pet package，当前仓库默认值为 `kitty.codex-pet`

3. `vector`
   - Vector 机器人能力开关

4. `category_routing`
   - 角色到 provider/profile 的映射配置
   - 关键字段：
     - `categories`
     - `role_model_map`

5. `active_provider`
   - 运行时主 provider 名称

6. `providers`
   - provider 列表
   - 每个 provider 定义：
     - `api_key`
     - `openai_base_url`
     - `model`
     - `context_limit_tokens`
     - `thinking_mode`
     - `max_output_tokens`
     - `request_timeout_ms`

7. `feishu`
   - 飞书应用配置

8. `audio`
   - 音频录制与播放相关配置

9. `mips`
   - 嵌入式硬件相关参数

### 8.3 路由配置说明

模型选择以角色路由为核心：

- `FAST`
- `PLANNER`
- `EXECUTOR`
- `REVIEWER`

示例含义如下：

```json
"category_routing": {
  "categories": {
    "deep": "deepseek_anthropic",
    "quick": "deepseek_anthropic"
  },
  "role_model_map": {
    "FAST": "quick",
    "ORACLE": "deep",
    "IMPLEMENT": "deep"
  }
},
"active_provider": "deepseek_anthropic"
```

说明如下：

- `FAST` 角色走 `quick`
- `PLANNER / EXECUTOR / REVIEWER` 走 `deep`
- 若某角色没有命中 `role_model_map`，则回退到 `active_provider`

## 9. 本机运行时目录说明

本机运行根目录为：

- `/home/wangergou/.agent-data/spiffs_data`

目录结构包括：

```text
spiffs_data/
├── ca/
├── cache/
├── config/
├── memory/
├── sessions/
├── skills/
├── web/
├── workspace/
└── *.codex-pet/
```

各目录作用如下：

| 目录 | 作用 |
|------|------|
| `config/` | 运行时配置、身份和规则文档 |
| `memory/` | 日志、摘要、facts 等 memory 数据 |
| `sessions/` | 会话记录 |
| `cache/` | 缓存数据，如飞书图片缓存 |
| `skills/` | 运行时技能目录 |
| `web/` | Web 前端静态资源 |
| `workspace/` | 工作区探测和运行期临时工作目录 |
| `ca/` | 证书文件 |

本机已观测到的目录包括：

- `cache/feishu_images`
- `skills/docx`
- `skills/pdf`
- `skills/pptx`
- `skills/xlsx`
- `skills/md-doc-writer`
- `guga.codex-pet`
- `kitty.codex-pet`
- `luo-xiaohei.codex-pet`

## 10. 安装、部署与启动

### 10.1 安装脚本

安装脚本位于：

- [install.sh](/home/wangergou/code/github/daima-agent/install.sh)

主要工作如下：

1. 执行 `make clean && make`
2. 创建 `~/.agent-data` 运行目录
3. 安装二进制到 `~/.agent-data/bin/agent`
4. 拷贝证书、Web 资源、技能资源、pet 资源
5. 安装配置模板与配置文件
6. 确保 `~/.bashrc` 中存在 PATH 片段
7. 停掉旧的开发/安装实例，启动 `~/.agent-data/bin/agent`
8. 对 `http://127.0.0.1:<web_port>/health` 做健康检查

### 10.2 安装后关键文件

安装后重点关注：

- `~/.agent-data/bin/agent`
- `~/.agent-data/spiffs_data/config/config.json`
- `~/.agent-data/spiffs_data/config/AGENTS.md`
- `~/.agent-data/spiffs_data/web/`
- `~/.agent-data/spiffs_data/skills/`

### 10.3 启动方式

常见启动方式：

1. 编译
   - `make`

2. 安装
   - `./install.sh`

3. 运行
   - 安装版：`agent`
   - 开发版：仓库内 `./run.sh`
   - 开发版后台启动：`./run.sh --background`

注意：

- `agent` 启动的是 `~/.agent-data/bin/agent`
- `./run.sh` 启动的是仓库内 `build-kbuild/agent`
- 两者默认共用 `web_port`，不要同时运行，否则浏览器看到的页面和实际连接的后端可能不是同一个实例

### 10.4 启动前检查项

建议检查以下内容：

- `config.json` 中的 `active_provider` 是否有效
- 对应 provider 的 `api_key`、`base_url`、`model` 是否配置正确
- `web_port` 是否被占用
- 通道配置是否完整
- 若启用 Feishu、Vector 或音频能力，相关配置是否齐全

## 11. 日志、会话与问题定位

### 11.1 日志位置

本机日志重点关注：

- `/home/wangergou/.agent-data/spiffs_data/memory/agent.log`

该日志适合用于定位：

- LLM 请求失败
- tool 执行失败
- session / recovery 问题
- provider / model fallback 问题
- 字符编码或 JSON 组装问题

### 11.2 会话与持久化

会话数据主要位于：

- `spiffs_data/sessions/`
- `spiffs_data/memory/`

相关逻辑位于：

- `drivers/memory/session_store.c`
- `drivers/memory/session_store_file.c`
- `drivers/memory/session_store_file_summary.c`
- `drivers/memory/session_store_file_facts.c`

### 11.3 常见排查入口

1. 启动失败
   - 检查 `config.json`
   - 检查 `install.sh` 安装结果
   - 检查 `agent.log`

2. 模型不符合预期
   - 检查 `active_provider`
   - 检查 `category_routing.categories`
   - 检查 `role_model_map`

3. subagent 选模异常
   - 检查 `delegate_task` 是否触发
   - 检查 `PLANNER / EXECUTOR / REVIEWER` 的 `role_model_map`
   - 检查 `kernel/sched/agent.c`

4. 回复未正常回传
   - 检查 outbound bus
   - 检查 `channel_router`
   - 检查通道实现

5. 工具调用异常
   - 检查 `turn_exec.c`
   - 检查 `tool_runtime.c`
   - 检查具体工具实现文件

## 12. 维护与接手要点

这一章面向继续开发本项目的人，目标是降低理解成本和改动风险。

### 12.1 优先阅读顺序

建议按如下顺序阅读：

1. `README.md`
2. `docs/ARCHITECTURE.md`
3. `docs/SOFTWARE_MANUAL.md`
4. `init/main.c`
5. `init/bootstrap.c`
6. `kernel/loop.c`
7. `kernel/turn/turn_entry.c`
8. `kernel/turn/turn_prepare.c`
9. `kernel/turn/turn_pipeline.c`
10. `kernel/turn/turn_finish.c`

### 12.2 变更影响面判断

修改时可按如下方式判断影响范围：

- 改 prompt / history / message
  - 看 `turn_prompt_build.c`、`turn_message_build.c`
- 改执行主链
  - 看 `turn_entry.c`、`turn_pipeline.c`、`turn_finish.c`
- 改模型路由
  - 看 `turn_decision.c`、`router.c`、`sched/agent.c`
- 改工具执行
  - 看 `turn_exec.c`、`tool_runtime.c`
- 改配置加载
  - 看 `runtime*.c`、`config.json`
- 改部署与运行目录
  - 看 `install.sh`、`bootstrap.c`、`paths.c`

### 12.3 维护原则

建议持续保持以下原则：

- 默认主链只保留一条
- `drivers/` 负责能力，不反向定义主流程
- skill 摘要与 skill 工具生命周期分离
- subagent 只通过 `delegate_task + kernel/sched` 进入
- 同步回合临时资源由 `turn_io` 管理
- async resume 快照由 `turn_context` 管理

## 13. 关键文件索引

| 类别 | 文件 |
|------|------|
| 进程入口 | `init/main.c` |
| 启动初始化 | `init/bootstrap.c` |
| 外层循环 | `kernel/loop.c` |
| 单回合入口 | `kernel/turn/turn_entry.c` |
| 回合决策 | `kernel/turn/turn_decision.c` |
| 路由 | `kernel/router.c` |
| 准备阶段 | `kernel/turn/turn_prepare.c` |
| prompt 构建 | `kernel/turn/turn_prompt_build.c` |
| message 构建 | `kernel/turn/turn_message_build.c` |
| 执行阶段 | `kernel/turn/turn_pipeline.c` |
| interview | `kernel/turn/turn_interview.c` |
| LLM 工具循环 | `kernel/turn/turn_run.c` |
| 工具执行 | `kernel/turn/turn_exec.c` |
| 收尾阶段 | `kernel/turn/turn_finish.c` |
| reply 处理 | `kernel/turn/turn_reply.c` |
| post actions | `kernel/turn/turn_post.c` |
| 持久化 | `kernel/turn/turn_persist.c` |
| subagent 调度 | `kernel/sched/core.c` |
| subagent 启动 | `kernel/sched/agent.c` |
| 工具代理入口 | `drivers/tool/tool_delegate.c` |
| 工具运行时 | `drivers/tool/tool_runtime.c` |
| LLM 接口 | `drivers/llm/llm_proxy.h` |
| Host LLM 实现 | `arch/host/llm_proxy_host.c` |
| 配置模板 | `spiffs_data/config/config.json` |
| 安装脚本 | `install.sh` |
