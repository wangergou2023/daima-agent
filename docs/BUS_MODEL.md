# Daima Agent: Bus/Driver/Device 总线模型

将 Linux 内核的 bus/driver/device 模型移植到 AI Agent 架构，**市面唯一**使用总线模型实现组件解耦的 AI Agent 框架。

## 实现状态

```
核心 struct + API         ✅  include/linux/bus.h + ipc/bus_device.c
3 条总线实例               ✅  ipc/bus_init.c
tool_bus (25 个工具)       ✅  catch-all match → tool_generic driver
channel_bus (4 个通道)     ✅  feishu/vector/voice/gateway → name match
llm_bus (2 个协议驱动)      ✅  openai_compatible + anthropic_compatible
skill_module (三层模型)     ❌ 概念已定义，待实现
JSON 设备树解析             ❌ 待实现
热插拔 reprobe 链          ❌ 待实现
```

## 核心概念

### 三层模型

和 Linux 内核一样，Agent 架构也分三层：

| 层 | 内核对应 | Agent 对应 | 是什么 |
|----|---------|-----------|--------|
| 设备层 | `struct device` | `struct device` | **被发现的事实**。不是代码，只声明"我存在，我需要 XXX"。几乎零代码量。 |
| 驱动层 | `struct driver` | `struct driver` | **执行逻辑**。全是代码：怎么 probe、怎么执行、怎么 remove。 |
| 模块层 | `struct module` / `insmod` | `struct skill_module` | **容器**。打包一组 device，管理加载/卸载/跨 bus 依赖。 |

### 总线匹配

| 内核概念 | Agent 实现 | 含义 |
|----------|-----------|------|
| `bus_type` | `bus_type` | 匹配规则 + 注册表 |
| `device` | `device` | 能力声明 ("我叫 xxx") |
| `driver` | `driver` | 能力实现 ("我能做 xxx") |
| `probe()` | `probe()` | 检查依赖是否满足 → 绑定 |
| device tree | `spiffs_data/` | JSON 文件描述设备资源 |
| name match | name match | 字符串精确匹配 |
| `insmod/rmmod` | `skill_module.{load,unload}` | 加载/卸载一组设备 |

## 设计原则

**全部字符串匹配。没有语义。Agent 不需要"理解"工具。**

Agent 拿到的工具和读文件一样：

```
read_file    → tool_bus → name match → probe → execute
write_file   → tool_bus → name match → probe → execute
pptx         → tool_bus → name match → probe(检查terminal是否可用) → execute
webfetch     → tool_bus → name match → probe → execute
```

## 三条总线

```
┌─ tool_bus ─────────────────────────────────────────────┐
│ 所有可被 Agent 调用的工具                                 │
│                                                        │
│ 普通工具:                                               │
│   device: {name:"read_file", schema:{}}                 │
│   driver: tool_files_read.probe() → execute             │
│   probe: 直接 bind (无依赖)                              │
│                                                        │
│ skill 工具 (带依赖):                                     │
│   device: {name:"pptx",                                 │
│            dependencies:[{bus:"tool",name:"terminal"}]} │
│   driver: skill_pptx.probe()                            │
│   probe: 检查 tool_bus 是否有 "terminal" → bind         │
│                                                        │
│ match: 字符串精确匹配 (类似 platform_bus)                 │
└────────────────────────────────────────────────────────┘

┌─ channel_bus ─────────────────────────────────────────┐
│ 消息通道 (不暴露给 Agent)                                │
│                                                        │
│ device: {name:"feishu", app_id:"xxx"}                   │
│ driver: feishu_channel.probe()                          │
│ match: 字符串精确匹配                                    │
└────────────────────────────────────────────────────────┘

┌─ llm_bus ─────────────────────────────────────────────┐
│ LLM 后端 (不暴露给 Agent)                                │
│                                                        │
│ device: {model:"deepseek-v4", url:"http://..."}         │
│ driver: openai_compatible.probe()                       │
│ match: model + endpoint 匹配                            │
└────────────────────────────────────────────────────────┘
```

## Skill 不是设备，也不是驱动

Skill（如 pptx）是**模块层**的东西——它既不是 device 也不是 driver，而是它们的容器。

### 为什么需要第三层

```
一个 Skill "pptx" 要往 tool_bus 上挂多个 tool:
  pptx_generate  → 需要 python
  pptx_to_pdf    → 需要 libreoffice
  pptx_thumbnail → 需要 imagemagick

这些 tool 共享依赖，需要统一的加载/卸载生命周期。
```

### skill_module — 对应内核 `struct module`

```c
struct skill_module {
    const char *name;              // "pptx"
    const char *description;       // from SKILL.md front matter
    struct device *devices;        // 要注册到 tool_bus 的 device 列表
    int device_count;
    struct dependency *deps;       // 跨 bus 的全局依赖

    int (*probe)(void);            // 检查所有依赖的 tool 是否就位
    int (*load)(void);             // 注册所有 device 到对应 bus
    void (*unload)(void);          // 卸载所有 device
};
```

### 三层调用链

```
用户说"做个PPT"
  │
  ├── skill_router: 匹配到 skill_module "pptx"
  │
  ├── skill_module->probe()
  │     └── bus_device_exists("tool_bus", "terminal")    → ✅
  │     └── bus_device_exists("tool_bus", "files")       → ✅
  │     └── 全满足 → return 0
  │
  ├── skill_module->load()
  │     ├── device_register("pptx_generate", tool_bus)
  │     │     └── bus_probe: match → driver "terminal_exec"
  │     ├── device_register("pptx_to_pdf", tool_bus)
  │     │     └── bus_probe: match → driver "libreoffice_executor"
  │     └── LLM 工具列表自动增加这 2 个 tool
  │
  └── turn 结束: skill_module->unload()
        ├── device_unregister("pptx_generate")
        └── device_unregister("pptx_to_pdf")
```

### 对比: Skill 不是 driver

```
❌ 错误理解:
  skill = driver (probe → execute)

✅ 正确理解:
  skill_module "pptx"          ← 容器层：加载/卸载/依赖管理
    ├── device "pptx_generate"  ← 声明层
    │    └── driver "terminal_exec"           ← 执行层
    └── device "pptx_to_pdf"    ← 声明层
         └── driver "libreoffice_executor"  ← 执行层
```

普通 tool（weather, terminal）没有模块层——它们是单 device，不需要容器管理。只有 skill 这种"一组 tool + 共享依赖"的才需要 skill_module。

## 数据结构

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

struct dependency {
    const char *bus_name;   // "tool_bus"
    const char *dev_name;   // "python"
};

struct driver {
    const char *name;
    struct bus_type *bus;
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    void *ops;              // execute 函数等
};

struct skill_module {
    const char *name;              // "pptx"
    const char *description;       // 来自 SKILL.md front matter
    struct device *devices;        // 要注册的 device 列表
    int device_count;
    struct dependency *deps;       // 跨 bus 全局依赖
    int dep_count;

    int (*probe)(void);            // 检查所有依赖是否就位
    int (*load)(void);             // 注册所有 device
    void (*unload)(void);          // 卸载所有 device
};
```

## 设备树 (spiffs_data/)

统一入口文件，一个函数路由到所有 bus：

```json
// device.json
{
  "devices": [
    // tool bus - 普通工具
    {"bus":"tool", "name":"read_file",  "driver":"tool_files_read",  "schema":{...}},
    {"bus":"tool", "name":"write_file", "driver":"tool_files_write", "schema":{...}},
    {"bus":"tool", "name":"terminal",   "driver":"tool_terminal",    "schema":{...}},

    // tool bus - skill (带依赖，依赖 tool_bus 上的 terminal/files)
    {"bus":"tool", "name":"pptx",
     "driver":"skill_pptx",
     "dependencies":[{"bus":"tool","name":"terminal"}, {"bus":"tool","name":"files"}]},

    // llm bus
    {"bus":"llm", "name":"deepseek-v4",
     "driver":"openai_compatible",
     "url":"http://10.3.20.46:4000",
     "model":"deepseek-v4-pro"}
  ]
}
```

解析时自动路由：

```c
// of_populate_all("spiffs_data/device.json")
//   {"bus":"tool", ...} → tool_bus.add_device()
//   {"bus":"llm", ...}  → llm_bus.add_device()
//   {"bus":"llm", ...}  → llm_bus.add_device()
```

## 生命周期管理

### 热插拔

```c
// 安装: terminal 上线 → pptx 自动可用
tool_bus.add_device(device_terminal)
  → bus_probe(&tool_bus, device_terminal)  ← terminal 绑定
  → bus_reprobe(&tool_bus, "pptx")         ← 重新 probe pptx
  → probe OK → bind ✅
  → Agent 工具列表自动出现 pptx

// 卸载: terminal 下线 → pptx 自动消失
tool_bus.remove_device("terminal")
  → tool_bus.each_device(depends_on("tool", "terminal"))
  → driver.remove() → unbind
  → Agent 工具列表自动移除 pptx
```

### Probe 失败不清理设备

和内核一模一样 —— probe 失败的设备保留在总线上，等依赖满足后自动重新绑定：

```c
// terminal 不存在时
probe(pptx) → bus_device_exists("tool", "terminal") → false
  → return -ENODEV
  → dev->drv = NULL          ← 设备保留，不清理
  → pr_warn("pptx: probe failed, waiting for terminal")

// terminal 安装后
tool_bus.add_device(terminal)
  → tool_bus.reprobe("pptx")
  → bus_device_exists("tool", "terminal") → true ✅
  → probe OK → bind ✅
```

类比：`/dev/sda` 没插盘时驱动 probe 失败，设备节点还在，盘插上自动绑定。

## 初始化流程

```c
// 1. 总线创建
bus_create(&tool_bus);     // Agent 可调用的工具
bus_create(&channel_bus);  // 消息通道
bus_create(&llm_bus);      // LLM 后端

// 2. 驱动注册
driver_register(&tool_files_read_driver, &tool_bus);
driver_register(&skill_pptx_driver, &tool_bus);

// 3. 设备树解析（普通 tool）
parse_device_tree("spiffs_data/tools/device.json", &tool_bus);

// 4. Skill 模块加载（通过 skill_module）
skill_module_load(&skill_pptx);
//   ↓ 内部流程:
//   skill_module->probe()
//     → bus_device_exists("tool_bus", "terminal")    ✅
//     → bus_device_exists("tool_bus", "files")       ✅
//   skill_module->load()
//     → device_register("pptx_generate", tool_bus)
//     → device_register("pptx_to_pdf", tool_bus)
//     → bus_probe_all(tool_bus)

// 5. 自动 probe
bus_probe_all(&tool_bus);
// 结果:
//   read_file      → bind ✅
//   pptx_generate  → bind ✅  (python 可用)
//   pptx_to_pdf    → bind ✅  (libreoffice 可用)
//   pdf_generate   → probe 失败 ❌ (libreoffice 不可用)

// 6. 模块卸载 — turn 结束时
skill_module_unload(&skill_pptx);
//   → device_unregister("pptx_generate")
//   → device_unregister("pptx_to_pdf")
//   → LLM 工具列表自动移除这 2 个 tool
```

## 为什么是创新

### 竞品对比

| 维度 | Daima Agent | OpenAI plugins | Anthropic MCP | LangChain |
|------|-------------|---------------|---------------|-----------|
| 架构 | **bus/driver/device** | manifest.json | JSON-RPC | Python class |
| 内核验证 | ✅ 30 年 Linux | ❌ | ❌ | ❌ |
| 设备驱动分离 | ✅ 完全解耦 | ❌ 混在一起 | ❌ 混在一起 | ❌ 混在一起 |
| 依赖验证 | ✅ probe() 自动检查 | ❌ | ❌ | ❌ |
| 热插拔 | ✅ 装即用/卸即消失 | ❌ 重新部署 | ❌ 重启 | ❌ 重启 |
| 匹配方式 | 字符串精确 | 语义匹配 | 协议匹配 | 函数名 |
| 运行时 | C 单二进制 | Node.js | Node/Python | Python |

### 解耦方式的本质区别

```
OpenAI plugin:
  plugin = manifest + code  ← 一体，改一行重新部署

Daima Agent:
  tool_bus:    "你叫 pptx? device 在 bus 上，driver 也在 bus 上"
  skill_pptx:  "我有依赖 [terminal]，probe 时检查"

  两层互不感知 —— 改 terminal 路径不改 pptx 一行代码
```

### 核心优势

1. **无人做过**: AI Agent 领域没有 bus/driver/device 模型
2. **内核验证**: 这个模型在 Linux 内核里支撑了 30 年，不是实验性设计
3. **全部字符串匹配**: Agent 不需要语义理解，bus 精确匹配
4. **skill = tool + dependencies**: 唯一特殊之处是 probe 时多一步依赖检查
5. **热插拔**: 依赖上线 → 自动可用，依赖下线 → 自动消失

## 架构流程图

### 启动顺序

```
main()
  └─ do_basic_setup()
       ├── message_bus_init()         ← 消息队列总线（入站/出站）
       ├── memory_store_init()
       ├── session_store_init()
       ├── skill_loader_init()
       ├── cron / heartbeat / proxy
       │
       ├── bus_init()                 ← 创建 3 条设备总线实例
       │    ├── tool_bus     (catch-all match)
       │    ├── channel_bus  (name match)
       │    └── llm_bus      (name match)
       │
       ├── bus_channel_register_all() ← 通道驱动注册 + probe 初始化
       │    ├── feishu  → feishu_bot_init() + feishu_bot_start()
       │    ├── vector  → vector_channel_init()
       │    ├── voice   → voice_channel_init()
       │    └── gateway → (no-op)
       │
       └── bus_llm_register_all()     ← LLM 协议驱动注册
            ├── openai_compatible
            └── anthropic_compatible

  └─ llm_proxy_init()                 ← 读运行时配置（API key / model / URL）
  └─ tool_registry_init()             ← 25 个工具注册到 tool_bus
       ├── register_tool() → s_tools[]   (原数组，兼容)
       └── device_register() → tool_bus  (新总线)
```

### 总线架构

```
                    ┌──────────────────────────────────────────┐
                    │              LLM Proxy                   │
                    │    llm_proxy_init() + llm_chat_tools()   │
                    └──────────────┬───────────────────────────┘
                                   │ 读配置，独立于总线
                    ┌──────────────┴───────────────────────────┐
                    │                                         │
    ┌───────────────┴──────────────┐    ┌──────────────────────┴──────────────┐
    │        tool_bus              │    │           channel_bus                │
    │  match: catch-all            │    │  match: name match                   │
    │                              │    │                                      │
    │  drivers:                    │    │  drivers:                            │
    │    tool_generic              │    │    feishu   → bot_init + bot_start   │
    │                              │    │    vector   → channel_init           │
    │  devices:                    │    │    voice    → channel_init           │
    │    weather  ──bound──┐       │    │    gateway  → (no-op)               │
    │    files    ──bound──┤       │    │                                      │
    │    terminal ──bound──┤       │    │  devices:                            │
    │    todo     ──bound──┼──→ tool_generic   │    feishu   ──bound──→ feishu         │
    │    webfetch ──bound──┤       │    │    vector   ──bound──→ vector         │
    │    ... 25 tools ────┘       │    │    voice    ──bound──→ voice          │
    │                              │    │    gateway  ──bound──→ gateway        │
    └──────────────────────────────┘    └──────────────────────────────────────┘

    ┌──────────────────────────────┐    ┌──────────────────────────────────────┐
    │           llm_bus             │
    │  match: name match            │
    │                               │
    │  drivers:                     │
    │    openai_compatible          │
    │    anthropic_compatible       │
    │                               │
    │  devices: (模型实例，待注册)     │
    │    deepseek-v4 → openai       │
    └──────────────────────────────┘
```

### Probe 流程

```
device_register(dev, bus)
  │
  ├── list_add → bus->devices
  │
  └── bus_probe(bus, dev)
        │
        └── for each driver in bus->drivers:
              │
              ├── match(dev, drv) ?
              │     ├── 0 (匹配)
              │     │     ├── drv->probe ?
              │     │     │     ├── 有 → probe(dev)
              │     │     │     │         ├── 0  → dev->drv = drv ✅ bind
              │     │     │     │         └── ≠0 → 设备保留在总线，不绑定 ⚠️
              │     │     │     └── 无 → dev->drv = drv ✅ bind (no probe)
              │     │     │
              │     └── ≠0 (不匹配) → 继续下一个 driver
              │
              └── 无 driver 匹配 → 设备保留在总线，等待 driver 注册
```

### 三层架构 (Skill Module 生命周期)

```
                        ┌─────────────────────────┐
                        │    skill_module "pptx"   │  ← 容器层
                        │  probe() → 检查 tool 依赖 │
                        │  load()  → 注册所有 device│
                        │  unload()→ 卸载所有 device│
                        └──────────┬──────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
    ┌─────────┴─────────┐ ┌───────┴──────────┐ ┌───────┴──────────┐
    │ device            │ │ device           │ │ device           │  ← 声明层
    │ "pptx_generate"   │ │ "pptx_to_pdf"    │ │ "pptx_thumbnail"  │
    │ deps: [python]    │ │ deps: [libreoff] │ │ deps: [imagemag]  │
    └────────┬──────────┘ └───────┬──────────┘ └───────┬──────────┘
             │                    │                    │
    tool_bus ┤            tool_bus ┤            tool_bus ┤
             │                    │                    │
    ┌────────┴──────────┐ ┌───────┴──────────┐ ┌───────┴──────────┐
    │ driver            │ │ driver           │ │ driver           │  ← 执行层
    │ "terminal_exec"   │ │ "terminal_exec"  │ │ "terminal_exec"   │
    └───────────────────┘ └──────────────────┘ └───────────────────┘
             │                    │                    │
             └────────────────────┼────────────────────┘
                                  │
                         ┌────────┴──────────┐
                         │     tool_bus       │  ← 这些 driver 挂在
                         │ terminal/files/... │     tool_bus 上
                         └───────────────────┘
```

### 当前文件结构

```
ipc/
├── bus.h                消息总线（队列型，入站/出站）
├── bus.c                消息总线实现
├── bus_device.c   ✨    设备总线核心（10 个 API 函数）
├── bus_init.c     ✨    3 条总线实例创建
├── bus_channel.c  ✨    通道驱动注册
├── bus_llm.c      ✨    LLM 协议驱动注册
└── Makefile

include/linux/
├── bus.h          ✨    bus_type / device / dependency / driver 结构体
├── driver.h       ✨    增强 struct driver（bus 指针 / probe(device*)）
├── list.h              内核链表
├── slab.h              kmalloc / kfree
└── printk.h            pr_info / pr_err / pr_warn
```
