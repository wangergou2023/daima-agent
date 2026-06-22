# Daima Agent 架构文档

C11 + Kbuild 单二进制 AI Agent。Linux 内核 1:1 目录映射，bus/driver/device 总线模型实现组件解耦。

---

## 目录结构

```
daima-agent/
├── init/                    启动引导 (main.c → bootstrap.c)
├── kernel/                  核心子系统 (loop/sched/plan/printk)
│   ├── sched/               多 Agent 调度器
│   ├── time/                Cron 定时器
│   └── printk/              内核风格日志
├── drivers/                 驱动层
│   ├── tool/                34 个工具驱动
│   ├── llm/                 LLM 协议 (OpenAI/Anthropic)
│   ├── channel/             通道 (飞书/gateway/vector)
│   ├── memory/              会话/内存存储
│   └── skill/               技能容器模型
├── ipc/                     总线模型 + 消息队列 + 核间 IPC
├── extensions/              8 个 LKM 风格扩展模块
├── include/linux/           内核风格头文件 (17 个)
├── arch/{host,mips,arm}/    平台抽象
├── lib/net/fs/              工具库 / HTTP-TLS / 文件系统
├── scripts/                 Kbuild 引擎 (7 行 Kbuild.include)
├── test/                    48 单元测试
└── spiffs_data/             运行时数据 (配置/技能/证书)
```

---

## 启动流程

`main()` → 4 阶段启动 (`init/main.c`):

### 阶段 1: 运行时准备

```
bootstrap_prepare_runtime()
  ├── paths_init()            路径检测（HOME + SPIFFS 布局）
  ├── ensure_spiffs_layout()  目录创建（10 个目录树）
  └── runtime_config_init()   加载 config.json
```

### 阶段 2: 4 级手动 initcall 链

`do_basic_setup()` (`init/bootstrap.c`) 手动按序调用：

| 级别 | 函数 | 职责 |
|------|------|------|
| **core(1)** | `message_bus_init` → `core_ipc_init` → `agent_hooks_init` → `extensions_init` | IPC 基础设施 + 扩展注册 |
| **postcore(2)** | `memory_store_init` → `session_store_init` | 持久化服务 |
| **subsys(3)** | `cron_service_init` → `heartbeat_init` → `http_proxy_init` → `skill_loader_init` | 子系统服务 |
| **device(4)** | `bus_init` → `bus_channel_register_all` → `bus_llm_register_all` → `executor_core_start` → `memory_core_start` | 设备总线 + 多核启动 |

`extensions_init()` 按序注册 8 个扩展：intent → roles → plan → router → interview → sched → team → ralph

### 阶段 3: 驱动初始化

```
llm_proxy_init()          加载 LLM API 配置
tool_builtin_bus_init()   注册 34 个内置工具到 tool_bus
agent_loop_init()         启动上下文压缩线程 + 学习审阅
```

### 阶段 4: 服务启动

```
channel_router_start()    飞书 / Vector / WebSocket 通道路由
agent_loop_start()        创建 Agent 主循环线程
ws_server_start()         WebSocket 服务器 (port 1234)
```

---

## 总线模型 (Bus/Driver/Device)

借鉴 Linux 内核 bus/driver/device 模型，实现组件声明与执行分离。

### 三层架构

| 层 | 结构体 | 职责 |
|----|--------|------|
| **总线层** | `bus_type` | 匹配规则 + devices/drivers 双链表 |
| **设备层** | `struct device` | 能力声明 ("我叫 xxx")，几乎零代码 |
| **驱动层** | `struct driver` | 执行逻辑，包含 `probe()` / `remove()` |

### 三条总线

```
tool_bus:    34 个工具，name match 一对一绑定
channel_bus: 4 个通道 (feishu/vector/voice/gateway)
llm_bus:     2 个协议驱动 (openai_compatible + anthropic_compatible)
```

### 数据结构

```c
struct bus_type {
    const char *name;
    int (*match)(struct device *dev, struct driver *drv);
    struct list_head devices;
    struct list_head drivers;
};

struct device {
    const char *name;
    struct bus_type *bus;
    struct dependency *dependencies;  // skill 才有
    void *data;
    struct driver *drv;               // NULL = 未绑定
};

struct driver {
    const char *name;
    struct bus_type *bus;
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    void *ops;
};

struct dependency {
    const char *bus_name;
    const char *dev_name;
};
```

### Probe 流程

```
device_register(dev, bus)
  └── bus_probe(bus, dev)
        └── for each driver in bus->drivers:
              ├── match(dev, drv) ? 0
              │     ├── probe(dev) ? 0 → dev->drv = drv ✅
              │     └── probe 失败 → 设备保留在总线，等待依赖
              └── 无匹配 → 设备保留，等待 driver 注册
```

`struct driver` 必须是嵌入结构体的首字段 (`container_of` 从链表节点还原父结构体)。

### 热插拔

Probe 失败的设备保留在总线上。依赖上线后通过 `bus_reprobe()` 自动重试：

```
terminal 不存在 → pptx probe 失败 → 设备保留
terminal 安装   → bus_reprobe("pptx") → probe OK → 绑定 ✅
```

### Skill 模型 (三层容器)

Skill（如 pptx）是容器层——不是 device 也不是 driver，而是打包一组 device 的模块：

```
skill_module "pptx"            ← 容器层：加载/卸载/依赖管理
  ├── device "pptx_generate"   ← 声明层
  │    └── driver "terminal_exec"     ← 执行层
  └── device "pptx_to_pdf"     ← 声明层
       └── driver "terminal_exec"     ← 执行层
```

```c
struct skill_module {
    const char *name;
    struct device *devices;
    int device_count;
    struct dependency *deps;

    int (*probe)(void);      // 检查所有依赖是否就位
    int (*load)(void);       // 注册所有 device 到 tool_bus
    void (*unload)(void);    // 卸载所有 device
};
```

调用链： `probe() → bus_device_exists() → load() → device_register() × N → bus_probe_all()`

普通 tool（weather, terminal）是单 device，不需要 skill_module。只有"一组 tool + 共享依赖"才需要。

---

## Agent 主循环 + Turn 流水线

### 主循环

```
agent_loop_task() 循环:
  ├── turn_resume_poll()           优先处理执行核回复
  │     └── 工具执行回复 → 注入 tool_result，继续 LLM 循环
  └── message_bus_pop_inbound()     收到用户消息
        └── process_new_message()
              ├── agent_self_test()    (!test 检测)
              ├── turn_prepare()       同步加载 history
              └── agent_run_prepared_turn()
```

### Turn 流水线

```
消息入站
  ├── hooks_trigger_intent()     意图分类 (QA/IMPLEMENT/FIX/OPEN)
  ├── hooks_trigger_prepare()    plan + role 注入
  ├── turn_prepare()             构建 system prompt + 加载历史
  ├── hooks_trigger_before_run() 模型路由选择
  ├── turn_run()                 LLM 工具调用循环 (最多 20 轮)
  │     ├── llm_chat_tools() → LLM API
  │     ├── LLM 返回 tool_use → tool_bus_execute_for_channel()
  │     └── LLM 返回 text → 结束
  ├── turn_finish()              Ralph Loop 检查 + 持久化
  └── channel_router             分发回复到通道
```

---

## 多核调度 + IPC

### 三核分工

| 核 | ID | 职责 | 文件 |
|----|----|------|------|
| **Scheduler** | 0 | 主循环、意图分类、LLM 调用、调度 | `kernel/loop.c` |
| **Memory** | 1 | 会话持久化、上下文压缩 | `kernel/memory_core.c` |
| **Executor** | 2 | 工具执行 | `kernel/executor_core.c` |

### IPC 协议

```c
struct core_task {
    int id;
    int type;           // TASK_EXECUTE_TOOLS / TASK_SAVE_SESSION / TASK_COMPRESS_CONTEXT
    int status;
    char *payload;
    char *result;
    int timeout_ms;
};
```

通信：`core_send(core_id, &task)` → 队列 (depth 32) → `core_recv()` → `core_reply()`

调度核通过 fire-and-forget 分发任务到执行核和记忆核，非阻塞轮询核间回复。

### Subagent 调度

当 LLM 调用 `delegate_task` 工具时，触发多 Agent 并行调度：

```
delegate_task → sched_dispatch() → runqueue
  ├── PLANNER (class 0): 生成执行计划，不可写代码
  ├── EXECUTOR (class 1): 按计划执行代码
  └── REVIEWER (class 2): 审查执行结果
```

最多 4 agent 并行 (4 槽位 runqueue)，结果 merge 后返回 LLM。

---

## 工具系统

### 注册

```
tool_builtin_bus_init()
  ├── driver_register(&tool_xxx_driver()->drv, tool_bus)
  ├── register_builtin_tool(tool_xxx_definition())
  │     └── device_register(dev, tool_bus)       自动 probe 绑定
  └── tool_bus_tools_json() / tool_bus_tools_json_for_channel()
```

### 调用路径

```
LLM 返回 tool_use(call)
  → tool_runtime_execute_call(call, msg, output, size)
      ├── 输入补丁 (cron/channel 注入)
      ├── tool_bus_execute_for_channel()
      │     ├── channel 权限检查
      │     ├── tool_custom_execute()
      │     └── bus_find_device(tool_bus, name)
      │           ├── container_of → struct tool_driver
      │           └── driver->execute(input, output, size)
      └── 终端 sudo 密码重试
```

### 自定义工具

`spiffs_data/config/custom_tools.json`:

```json
{"name": "my_tool", "driver": "terminal_exec", "command": "my_prefix"}
```

零编译扩展——`driver` 复用已有驱动，`command` 前缀追加到 LLM 输入。

---

## 扩展系统

8 个 LKM 风格扩展，通过钩子链介入 Turn 流水线各阶段：

| 钩子 | 扩展模块 | 职责 |
|------|---------|------|
| `hooks_trigger_intent` | `module_intent` | LLM 意图分类 |
| | `module_roles` | 角色映射 |
| | `module_plan` | 生成执行计划 |
| `hooks_trigger_prepare` | `module_plan` | 注入计划到 prompt |
| | `module_roles` | 注入角色 prompt |
| `hooks_trigger_before_run` | `module_router` | 模型路由选择 |
| `hooks_trigger_replace_run` | `module_sched` | 多 Agent 调度 |
| | `module_interview` | Prometheus 访谈 |
| | `module_team` | Team Mode |
| `hooks_trigger_finish` | `module_ralph` | Ralph Loop 检查 |

`extensions_init()` 按序注册，`module_init()` 宏现为空（兼容保留）。`replace_run` 失败返回 `ERR_FAIL` 让钩子链继续，回退到默认 LLM 调用。

---

## 构建系统

### Kbuild 递归

每目录声明 `obj-y := file1.o file2.o`，子目录通过 `obj-y += subdir/`（末尾 `/` 必须）递归。

```
Makefile: core-y 目录列表 → scripts/Makefile.build → .c→.o → objects.link → ld → agent
```

### 关键命令

| 命令 | 说明 |
|------|------|
| `make` | → `build-kbuild/agent` |
| `make V=1` | 详细输出 |
| `make V=2` | 详细输出 + 重编译原因诊断 |
| `make clean` | 清理 |
| `make test` | 48 单元测试 |
| `make mips\|arm` | 交叉编译 |

### 编译选项

- `-std=gnu11 -Wall -Wextra`
- 架构：`ARCH=host` (默认) / `ARCH=mips` / `ARCH=arm`
- macOS Homebrew 自动探测 OpenSSL/curl 路径
- cJSON 独立编译 (`lib/cjson.c`, 3119 行)

---

## 自检系统

发送 `!test` 消息触发 10 个集成测试。`kernel/loop.c` 检测 `strncmp("!test", 5)` → 调用 `agent_self_test()` → 输出 JSON 结果。

| # | 测试 | 验证内容 |
|---|------|---------|
| 1 | executor queue + tool execution | 执行核任务 → 工具执行 → 回复 |
| 2 | message_bus push/pop | 消息队列入出站一致性 |
| 3 | tool_bus 6 key tools bound | weather/terminal/files/todo/webfetch/time 全部绑定 |
| 4 | memory queue + load | 记忆核加载不存在会话 → failed |
| 5 | real tool via executor | 工具经执行核执行 → 有效结果 |
| 6 | message pipeline | 入站→出站通道信息一致 |
| 7 | LLM end-to-end | agent_process_message → LLM API → 回复验证 |
| 8 | async compress | TASK_COMPRESS → 记忆核 → 压缩调度 |
| 9 | subagent dispatch | sched_dispatch → 3 agent 槽位 |
| 10 | delegate_task real | delegate_task 真实调 3 subagent LLM |

前提：`config.json` 配置有效 LLM API key。

---

## 设计原则

- **全部字符串匹配**：bus 不做语义理解，`strcmp` 精确匹配
- **声明/执行分离**：device 声明需求，driver 实现能力，互不感知
- **依赖显式化**：probe 阶段自动检查 `bus_device_exists()`
- **失败保留**：probe 失败设备留在总线，依赖就绪自动 retry
- **container_of 内省**：链表节点 → 父结构体，无需额外指针

## 关键约束

- PLANNER 永不可写代码（`sched/class.c` prompt 强制）
- 子 Agent 不可递归委托（`tool_delegate.c` 禁止）
- Plan 不可含 TODO/TBD（`plan.c` 拒绝并重生成）
- struct driver 必须是嵌入结构体首字段（`container_of` 要求）
- 内存分配走 `kmalloc/kfree`，日志走 `pr_*`
