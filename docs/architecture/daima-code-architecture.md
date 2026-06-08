# Daima 项目代码分析

> 状态：当前实现说明。适用读者：需要理解后端框架、模块边界、主流程的开发者。相关代码：`main/`、`spiffs_data/`、`CMakeLists.txt`

> 更新时间：基于当前工作树重新整理。
> 这份文档重点描述当前代码结构、主流程、模块边界，以及最近几轮重构后的实际状态。

## 1. 项目定位

代马 Daima 是一个用 C 语言实现的 Host AI Agent 宿主程序，目标是：

- 在 Linux Host 上方便调试和迭代
- 在 MIPS 设备上保留可移植能力
- 提供 Web、飞书、语音等多通道交互
- 通过 OpenAI-compatible 协议接入大模型
- 支持工具调用、会话记忆、上下文压缩、定时任务与技能扩展

它不是一个“单纯聊天壳子”，而是一个偏工程化的 Agent Runtime：

- 有明确的消息总线
- 有独立的 Agent 主循环
- 有工具注册与执行层
- 有会话持久化与上下文压缩
- 有平台通道适配层

## 2. 当前规模概览

基于当前源码树统计：

| 属性 | 当前情况 |
| :-- | :-- |
| 项目名称 | Daima Host AI Agent |
| 主要语言 | C11 |
| `main/` 下 `.c/.h` 文件数 | 136 |
| `main/` 下总代码量 | 约 18,872 行 |
| 构建系统 | CMake |
| 目标平台 | Linux Host + MIPS |
| 核心依赖 | libcurl / cJSON / OpenSSL / pthread |
| LLM 协议 | OpenAI-compatible only |

相较早期版本，当前代码已经明显从“大文件堆逻辑”逐步转向“按职责拆分模块”。

## 3. 当前目录结构

下面只列主干目录和关键文件，不追求穷举：

```text
main/
├── daima_host.c                     # 进程入口，服务初始化顺序
├── daima_config.h                   # 编译期宏配置
├── daima_text.c/.h                  # 通用字符串 helper
├── daima_base64.c/.h                # 通用 base64 helper
│
├── app/
│   ├── daima_bootstrap.c           # 最小启动参数检查与运行时准备
│   ├── daima_paths.c               # 解析 DAIMA_HOME / 可执行文件附近的数据目录
│   ├── daima_paths.h               # 运行时路径访问接口
│   ├── runtime_config.c            # 读取运行时 config.json
│   ├── runtime_config.h            # 运行时配置访问接口
│   ├── channel_router.c            # 出站路由启动
│   └── channel_runtime.c           # 通道能力封装（发消息/工具活动等）
│
├── agent/
│   ├── agent_loop.c                # Agent 总循环
│   ├── agent_turn_prepare.c        # 单轮准备：系统提示、历史、当前输入
│   ├── agent_turn_run.c            # 单轮运行：LLM + 工具调用循环
│   ├── agent_turn_exec_helpers.c   # 工具结果组装、自动验证、工具活动通知
│   ├── agent_turn_finish.c         # 单轮收尾
│   ├── agent_turn_common.c         # 共享消息语义、环境变量、清理逻辑
│   ├── agent_turn_persist.c        # 会话保存、出站回复、错误回复
│   ├── agent_prompt_debug.c        # prompt snapshot 落盘
│   ├── context_builder.c           # system prompt 拼装
│   ├── context_compressor.c        # 后台上下文压缩调度
│   ├── context_compress_ops.c      # 压缩执行细节
│   └── learning_review.c           # 后台学习复盘
│
├── bus/
│   └── message_bus.c               # 入站/出站消息队列
│
├── llm/
│   ├── llm_proxy_host.c            # LLM 主调度（OpenAI-compatible）
│   ├── llm_openai_payload.c        # OpenAI 请求体构建/响应解析
│   └── llm_http_client_host.c      # HTTP POST + payload 日志
│
├── memory/
│   ├── memory_store.c              # memory 文件读写
│   ├── session_store.c             # session 统一接口
│   └── session_store_file.c        # JSONL / summary / facts 文件后端
│
├── tools/
│   ├── tool_registry.c             # 工具注册表
│   ├── tool_runtime.c              # 工具调用运行时适配
│   ├── tool_files.c                # 文件工具定义（schema / definition）
│   ├── tool_file_read.c            # files action=read
│   ├── tool_file_query.c           # files action=list/search
│   ├── tool_file_mutations.c       # apply_patch / restore_file
│   ├── tool_file_paths.c           # 文件路径约束与解析
│   ├── tool_file_checkpoint.c      # 文件 checkpoint
│   ├── tool_system.c               # terminal 工具定义
│   ├── tool_terminal_exec.c        # 本地 shell 执行
│   ├── tool_cron.c                 # cron action=add/list/remove
│   ├── tool_todo.c                 # todo 工具
│   ├── tool_skills.c               # skills action=list/view
│   ├── tool_session_search.c       # session_search 入口
│   ├── tool_session_search_scan.c  # session_search 扫描
│   └── tool_session_search_render.c# session_search 渲染
│
├── gateway/
│   ├── ws_server_host.c            # WebSocket + 内嵌 Web UI
│   ├── ws_http_helpers.c           # HTTP 响应辅助
│   └── ws_client_session.c         # Web client session 管理
│
├── channels/feishu/
│   ├── feishu_bot.c                # 飞书门面层：初始化/启动/发送
│   ├── feishu_api.c                # 飞书 OpenAPI
│   ├── feishu_event_handler.c      # 飞书入站事件解析
│   ├── feishu_media.c              # 图片下载缓存
│   ├── feishu_message.c            # 文本/富文本内容规整
│   ├── feishu_ws_transport.c       # WS 传输层
│   ├── feishu_ws_runtime.c         # WS 长连接运行循环
│   └── feishu_ws_proto.c           # 飞书 WS 帧协议编解码
│
├── cron/
│   └── cron_service.c              # 定时任务服务
│
├── heartbeat/
│   └── heartbeat.c                 # 心跳任务
│
├── skills/
│   └── skill_loader.c              # skills 扫描/加载/摘要
│
├── voice/
│   ├── voice_channel.c             # 语音通道
│   ├── voice_wake_stub.c           # Host stub
│   └── voice_wake_mips.c           # MIPS 唤醒
│
├── audio/
│   ├── audio_io_stub.c             # Host stub
│   └── audio_io_mips.c             # MIPS 音频
│
├── vision/
│   ├── vision_capture_stub.c       # Host stub
│   └── vision_capture_mips.c       # MIPS 抓拍
│
├── platform/
│   ├── daima_platform.c            # 平台信息 / 内存查询
│   └── daima_os.c                  # 任务/队列/时间等 OS 封装
│
└── proxy/
    └── http_proxy_host.c           # HTTP 代理设置
```

## 4. 系统主流程

从运行时看，主链路可以概括为：

```text
输入通道
  Web UI / Feishu / Voice / Cron / Heartbeat / Internal Event
        │
        ▼
message_bus 入站队列
        │
        ▼
agent_loop
  ├─ prepare：构建 system prompt、会话摘要、facts、当前消息
  ├─ run：调用 LLM，执行多轮工具调用
  └─ finish：保存会话、触发压缩/复盘、推送最终回复
        │
        ▼
message_bus 出站队列
        │
        ▼
channel_router / channel_runtime
        │
        ├─ websocket
        ├─ feishu
        └─ voice / system
```

这个结构的好处是：

- 输入通道与 Agent 解耦
- 工具执行与通道层解耦
- 会话持久化与主循环解耦
- 压缩 / 学习复盘可以后台异步做，而不是阻塞当前回复

## 5. 启动顺序与运行时准备

入口是 `main/daima_host.c`。

当前启动顺序大致为：

1. 检查是否传入 `-h` / `--help`
2. 执行 `daima_bootstrap_prepare_runtime()`
3. 在 `daima_paths_init()` 中解析运行时 home 与数据目录
4. 在 `runtime_config_init()` 中读取 `config.json`
5. 根据运行时配置设置时区、Provider、音频/飞书等参数
6. 初始化消息总线、记忆、技能、会话存储、HTTP 代理
7. 初始化语音、飞书、LLM、工具、Cron、Heartbeat、Agent
8. 启动出站路由
9. 启动 Agent loop / cron / heartbeat / WebSocket server

`main/app/daima_bootstrap.c` 现在主要负责：

- 最小启动参数对应的 usage 输出
- 调用 `daima_paths_init()`，统一解析运行时 home 与数据目录
- 调用 `runtime_config_init()`，把 `config.json` 映射成运行期参数
- `OPENAI_BASE_URL`、模型、API key 等运行时覆盖
- 语音 / 飞书 / 唤醒 / 音频相关环境变量桥接
- SPIFFS 目录结构准备

值得注意的是：

- 当前已经去掉多协议运行时分支，LLM 路径就是 **OpenAI-compatible only**
- `DAIMA_*` 已经是唯一主路径，不再保留旧兼容入口
- 不再依赖 `daima_secrets.h` / `daima_secrets.h.example` 这类编译期 secrets 文件
- 除了 `--help` 外，不再支持业务型 CLI 参数；运行配置统一从 `config.json` 获取

### 5.1 运行时 home：参考 Hermes 的 `HERMES_HOME` 思路

当前 Daima 已经不再依赖“从哪个当前目录启动”。

运行时数据目录由 `main/app/daima_paths.c` 统一解析，优先级是：

1. `DAIMA_HOME`
2. `$HOME/.daima`
3. 无法取得 `HOME` 时，才兜底检查可执行文件附近是否存在 `spiffs_data/`

这意味着：

- 默认从任意目录运行 `daima` 都会使用同一份 `~/.daima/spiffs_data`
- 开发时如果需要临时使用仓库内数据目录，应显式设置 `DAIMA_HOME=/path/to/repo`
- Web 资源、sessions、memory、config、skills、cron 等路径都不再依赖 cwd

### 5.2 运行时配置：统一走 `config.json`

当前高收益的业务参数，已经集中收敛到 `DAIMA_HOME/spiffs_data/config/config.json`。

整体结构是：

```json
{
  "common": {},
  "active_provider": "ingenic_local",
  "providers": {
    "moonshot": {},
    "ingenic_local": {}
  },
  "feishu": {},
  "audio": {},
  "mips": {}
}
```

这里有几个关键点：

- `active_provider` 是“当前使用哪个 provider alias”
- `providers` 下一级的 key 是**用户自定义名字**，不是写死保留字
- 程序会先读 `active_provider`，再去取 `providers[active_provider]`
- 如果 `config.json` 不存在，程序只会提示参考 `config.example.json`，不会自动生成配置文件

当前已经迁入 `config.json` 的高价值运行时参数包括：

- `common.timezone`
- `common.context_limit_tokens`
- `common.session_max_msgs`
- `common.compress_trigger_msgs`
- `common.compress_keep_msgs`
- `common.web_port`
- `common.cron_check_interval_ms`
- `common.heartbeat_interval_ms`
- `providers.<alias>.api_key`
- `providers.<alias>.openai_base_url`
- `providers.<alias>.model`
- `providers.<alias>.context_limit_tokens`
- `providers.<alias>.thinking_mode`
- `providers.<alias>.needs_reasoning_content`
- `feishu.*`
- `audio.*`
- `mips.*`

这意味着：当前“怎么连模型、用哪个模型、是否带 thinking 字段、是否回传 reasoning_content、上下文窗口多大”，都应优先在 `config.json` 中调，而不是继续往代码里塞模型名判断。

## 6. Agent 主循环：当前最核心的部分

当前 `agent/` 目录是整个项目最值得关注的中枢。

### 6.1 `agent_turn_prepare.c`

职责：准备一轮 LLM 输入。

主要做这些事：

- 生成基础 system prompt
- 追加会话 summary
- 追加 facts 卡片
- 追加当前轮运行时上下文（channel、chat_id、source）
- 对不同消息来源做语义区分：
  - 用户消息
  - cron 触发事件
  - heartbeat 触发事件
  - internal 控制事件
- 如果开启视觉，并且当前输入带图片，则构造多模态输入
- 可选地把最终 prompt dump 到 markdown 文件，便于开发观察

这部分最近已经瘦身：

- 公共消息语义判断抽到了 `agent_turn_common.c`
- prompt dump 抽到了 `agent_prompt_debug.c`

### 6.2 `agent_turn_run.c`

职责：执行单轮 Agent 推理。

逻辑很清晰：

- 调用 `llm_chat_tools()`
- 若模型直接返回文本，则结束
- 若模型返回 tool calls，则：
  - 追加 assistant/tool_use 消息
  - 执行工具
  - 回填 tool_result
  - 继续下一轮
- 达到工具轮次上限时，强制生成最终回复
- 如本轮修改了代码但未显式验证，会尝试自动执行构建/验证命令

当前默认是典型的 ReAct 风格闭环。

### 6.3 `agent_turn_exec_helpers.c`

这是 Agent 运行期的辅助层，主要负责：

- 把 LLM 工具调用转换成 `tool_result`
- 统计这轮副作用（是否改了代码、是否已验证）
- 自动推断是否需要触发构建验证
- 给 Web / 通道层发送简洁的工具活动通知

它是“LLM ↔ 工具 ↔ Web 展示”之间的重要胶水层。

### 6.4 `agent_turn_finish.c` + `agent_turn_persist.c`

职责：收尾。

- 保存本轮入站消息与 assistant 回复到 session
- 推送最终回复到出站消息队列
- 根据情况触发：
  - 上下文压缩
  - 学习复盘
- 若本轮失败，则生成统一错误回复
- 清理当前消息占用的资源（文本 / 图片缓存文件）

这里最近也做了明显收口：

- 会话保存、出站回复、错误回复已拆到 `agent_turn_persist.c`
- `agent_turn_finish.c` 本身已经非常薄，只做流程编排

## 7. 上下文工程与记忆链路

代马 Daima 目前已经具备一个比较完整的上下文工程雏形。

### 7.1 会话存储

核心文件：

- `main/memory/session_store.c`
- `main/memory/session_store_file.c`

当前会话层支持：

- JSONL 聊天历史
- summary 文件
- facts 文件
- 会话记录枚举
- 按 chat_id 读写

### 7.2 上下文压缩

核心文件：

- `main/agent/context_compressor.c`
- `main/agent/context_compress_ops.c`

当前机制是：

- 不在当前回复链路里硬同步压缩
- 达到阈值后调度后台 worker 异步压缩
- 压缩后把较旧对话收敛为 summary
- 通过 protect_first / protect_last 等策略保护头尾消息

这比“每轮都同步压缩”更合理，也更接近真实产品体验。

### 7.3 facts 卡片

facts 的定位不是聊天全文摘要，而是：

- 用户稳定偏好
- 已确认约束
- 长期有效设定
- 对未来轮次有价值的事实

在 system prompt 中，facts 与 summary 分开注入，这是正确方向。

### 7.4 learning review

`main/agent/learning_review.c` 用于在后台做轻量复盘，属于“自我整理”能力的雏形。

它不是模型训练，而是运行时层面的经验沉淀。

## 8. LLM 层：当前是 OpenAI-compatible 单协议

当前 LLM 模块的关键文件：

- `main/llm/llm_proxy_host.c`
- `main/llm/llm_openai_payload.c`
- `main/llm/llm_http_client_host.c`

当前状态要点：

- 只保留 OpenAI-compatible 协议路径
- 支持工具调用
- 支持多模态图片输入
- `context_limit_tokens` 优先从 `config.json` 读取；未配置时回退到固定默认值 `128000`
- `thinking_mode` / `needs_reasoning_content` 改为配置驱动，不再按模型名猜测
- payload 日志支持预览/截断输出

最近的结构优化主要是：

- `llm_proxy_host.c` 更像“策略 + 主流程”
- HTTP POST、鉴权 header、payload 日志被下沉到了 `llm_http_client_host.c`
- 通用字符串与 base64 helper 已外提，避免 LLM / Feishu / Vision 各写一份

## 9. 工具系统：当前 13 个普通通道工具

工具注册表在 `main/tools/tool_registry.c`。

普通通道当前共注册 13 个工具；Vector/voice 通道另行暴露 `robot_*` 控制工具：

| 工具 | 作用 |
| :-- | :-- |
| `weather` | 查询天气 |
| `get_current_time` | 获取当前时间 |
| `files` | 统一文件查看：`action=read/list/search` |
| `apply_patch` | Codex 风格补丁，用于新建、修改、删除文件 |
| `restore_file` | 从 checkpoint 恢复文件 |
| `todo` | 待办列表 |
| `work_item` | 结构化事项收集 |
| `webfetch` | 获取网页内容 |
| `daima_log` | 读取 Daima 运行日志 |
| `skills` | 技能浏览：`action=list/view` |
| `session_search` | 搜索历史会话 / facts / summary |
| `cron` | 定时任务管理：`action=add/list/remove` |
| `terminal` | 执行本地命令 |

> `restore_file` 保持独立，因为它是失败回滚能力，不是常规修改入口。

### 9.1 文件工具的现状

文件工具最近做了较明显的瘦身：

- `tool_files.c` 只保留 schema / definition
- `tool_file_read.c`、`tool_file_query.c` 分担读取和查询逻辑
- `tool_file_mutations.c` 负责 apply_patch / restore
- `tool_file_paths.c` 负责路径安全约束
- `tool_file_checkpoint.c` 负责自动 checkpoint

这套拆分后，后续继续增强“让 Agent 改代码”会更稳。

### 9.2 terminal 工具

`terminal` 是当前最关键的执行型工具之一：

- 执行本地 shell 命令
- 支持超时
- 支持输出捕获
- 支持 sudo 场景
- 支持 Web 侧密码交互
- 支持 Web 顶栏切换 Plan / Build 安全模式
- 返回结构化结果，便于 LLM 二次判断

这已经不是简单 `system()`，而是接近一个简化版 terminal runtime。

Terminal 的安全策略、运行时配置和 Web API 见：[Terminal 安全模式](../features/terminal-security-modes.md)。

### 9.3 session_search

`session_search` 现在已经拆为三层：

- 入口层
- 扫描层
- 渲染层

它对后续“查看上下文压缩 / 事实卡片 / 历史命中”非常关键。

## 10. Web 通道

核心文件：

- `main/gateway/ws_server_host.c`
- `main/gateway/ws_http_helpers.c`
- `main/gateway/ws_client_session.c`

当前 Web 通道特性包括：

- 内嵌 Web UI
- WebSocket 双向聊天
- tool activity 简洁展示
- 上下文统计展示
- 主题切换
- sudo 密码输入交互
- HTTP + WS 共存

Web 侧现在已经不只是“控制台输出”，而是 Agent 的主要可视化调试入口之一。

## 11. 飞书通道

核心文件：

- `main/channels/feishu/feishu_bot.c`
- `main/channels/feishu/feishu_api.c`
- `main/channels/feishu/feishu_event_handler.c`
- `main/channels/feishu/feishu_media.c`
- `main/channels/feishu/feishu_ws_transport.c`
- `main/channels/feishu/feishu_ws_runtime.c`
- `main/channels/feishu/feishu_ws_proto.c`

当前飞书侧已经不是一个单文件怪物，而是分为：

- 门面层：启动 / 发消息
- API 层：拉 token、发消息、拉 WS 配置
- 入站事件层：处理 text / image / post
- 媒体层：下载图片并缓存
- WS transport 层：底层传输
- WS runtime 层：长连接循环
- proto 层：飞书帧协议解析

这部分已经明显比以前更可维护。

## 12. Cron、Heartbeat、Skills

### 12.1 Cron

`main/cron/cron_service.c` 支持：

- 一次性任务 `at`
- 周期任务 `every`
- JSON 持久化
- 到期后转为系统事件，再进入 Agent 主循环

### 12.2 Heartbeat

`main/heartbeat/heartbeat.c` 是一个周期性后台事件入口。

它本质上也是一种“系统消息源”，而不是用户消息。

### 12.3 Skills

`main/skills/skill_loader.c` 负责：

- 读取 `spiffs_data/skills/`
- 解析 `SKILL.md`
- 生成技能摘要注入 prompt

skills 的定位更像轻量知识包，而不是复杂插件系统。

## 13. 平台与可移植性

通过 `daima_os.h` / `daima_platform.h`，当前项目已经把一部分宿主能力抽象掉了：

- 任务 / 线程创建
- 延迟与时间
- 队列
- 内存统计
- 随机数

再加上：

- `audio_io_stub.c` / `audio_io_mips.c`
- `voice_wake_stub.c` / `voice_wake_mips.c`
- `vision_capture_stub.c` / `vision_capture_mips.c`

说明项目从一开始就不是“纯 Linux 专用小程序”，而是带着嵌入式迁移目标在设计。

## 14. 当前代码的优点

### 14.1 架构已经比早期稳定很多

尤其是最近几轮瘦身后，几个重点方向都更清楚了：

- Agent 主流程拆成 prepare / run / finish
- LLM 层拆出 HTTP client
- 文件工具拆成 definition / read / write / query / mutation
- session_search 拆成入口 / 扫描 / 渲染
- 飞书拆成门面 / API / WS transport / WS runtime / proto

### 14.2 很多“运行时工程”已经具备雏形

比如：

- prompt snapshot
- context compression
- facts 卡片
- tool activity
- auto verification
- sudo 交互
- learning review

这些东西加起来，更像一个 Agent harness，而不只是聊天接口。

### 14.3 边界正在变清晰

当前代码越来越接近：

- 通道层负责收发
- Agent 层负责编排
- 工具层负责执行
- memory 层负责持久化
- LLM 层负责协议调用

这对后续继续做“让 Agent 自主改代码”很重要。

## 15. 当前仍值得继续优化的点

### 15.1 `agent/` 仍然是最重要的热点区

虽然已经瘦了不少，但它仍然是全项目的中枢：

- system prompt 拼装
- tool result 回填
- 自动验证
- 压缩与复盘触发

后续若继续提高可维护性，这里仍是第一优先级。

### 15.2 memory / compression / learning review 还可以进一步统一

现在功能已经有了，但三者之间的边界和数据流仍有继续收口空间。

### 15.3 网关与通道层还可以继续清理

尤其是 Web 与 Feishu 都已经进入“真实使用中不断加功能”的阶段，时间久了容易再次熵增。

### 15.4 测试体系仍偏弱

当前项目更偏“人工联调 + 实机验证”，缺少系统化单元测试 / 集成测试。

对 C 项目来说，这不是小问题，尤其是：

- JSON 解析
- 文件修改工具
- session 存储
- cron 规则
- WS 协议解析

这些都很适合补可重复执行的测试。

## 16. 总结

当前的代马 Daima 已经不是一个“单文件拼起来的 Agent Demo”，而是一个逐步形成骨架的 C 语言 Agent Runtime。

它的核心特征是：

- 多通道输入
- OpenAI-compatible LLM 接入
- 工具调用闭环
- 会话持久化与上下文压缩
- 面向 Host 调试，同时保留 MIPS 可移植性

最近几轮重构带来的最大变化，不是功能变多，而是结构变清晰：

- 大文件被拆散
- 公共 helper 被收口
- Agent 主流程更像一个真正的“编排层”
- LLM / Tools / Feishu / Web 各自的边界比以前清楚很多

如果从后续演进角度看，代马 Daima 现在最值得继续投入的方向依然是：

1. 继续稳固 `agent/` 主流程
2. 继续收口 memory / compression / review 链路
3. 为工具执行与代码修改补更多自动验证能力
4. 控制结构熵增，避免重新回到“大文件堆逻辑”

这也是它从“能跑”走向“能长期演进”的关键。
