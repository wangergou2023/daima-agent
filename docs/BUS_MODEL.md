# Daima Agent: Bus/Driver/Device 总线模型

将 Linux 内核的 bus/driver/device 模型移植到 AI Agent 架构，**市面唯一**使用总线模型实现组件解耦的 AI Agent 框架。

## 实现状态

```
核心 struct + API         ✅  include/linux/bus.h + ipc/bus_device.c
4 条总线实例               ✅  ipc/bus_init.c
tool_bus (25 个工具)       ✅  catch-all match → tool_generic driver
channel_bus (4 个通道)     ✅  feishu/vector/voice/gateway → name match
llm_bus (2 个协议驱动)      ✅  openai_compatible + anthropic_compatible
mcp_bus                   ❌ 空，待实现
Skill 依赖 probe           ❌ 待实现（需 mcp_bus 先就位）
JSON 设备树解析             ❌ 待实现
热插拔 reprobe 链          ❌ 待实现
```

## 核心概念

| 内核概念 | Agent 实现 | 含义 |
|----------|-----------|------|
| `bus_type` | `bus_type` | 匹配规则 + 注册表 |
| `device` | `device` | 能力声明 ("我叫 xxx") |
| `driver` | `driver` | 能力实现 ("我能做 xxx") |
| `probe()` | `probe()` | 检查依赖是否满足 → 绑定 |
| device tree | `spiffs_data/` | JSON 文件描述设备资源 |
| name match | name match | 字符串精确匹配 |

## 设计原则

**全部字符串匹配。没有语义。Agent 不需要"理解"工具。**

Agent 拿到的工具和读文件一样：

```
read_file    → tool_bus → name match → probe → execute
write_file   → tool_bus → name match → probe → execute
pptx         → tool_bus → name match → probe(检查python MCP) → execute
webfetch     → tool_bus → name match → probe → execute
```

## 四条总线

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
│            dependencies:[{bus:"mcp",name:"python"}]}    │
│   driver: skill_pptx.probe()                            │
│   probe: 检查 mcp_bus 是否有 "python" → bind            │
│                                                        │
│ match: 字符串精确匹配 (类似 platform_bus)                 │
└────────────────────────────────────────────────────────┘

┌─ mcp_bus ─────────────────────────────────────────────┐
│ 底层执行能力 (不直接暴露给 Agent)                         │
│                                                        │
│ device: {name:"python", path:"/usr/bin/python3"}        │
│ driver: mcp_python.probe()                              │
│ match: 字符串精确匹配                                    │
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

## Skill 的特殊之处: 有依赖的 probe

普通 tool 的 probe 什么也不检查，skill 的 probe 会验证依赖是否满足：

```c
// 普通 tool: probe 直接 bind
static int tool_files_probe(struct device *dev) {
    return 0;
}

// skill tool: probe 检查依赖
static int skill_pptx_probe(struct device *dev) {
    if (!bus_device_exists("mcp_bus", "python"))
        return -ENODEV;   // python 不可用 → 不注册
    return 0;              // python 可用 → 注册为 Agent 工具
}
```

对 Agent 完全透明 —— python 不可用时，pptx 直接从工具列表消失。

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
    const char *bus_name;   // "mcp_bus"
    const char *dev_name;   // "python"
};

struct driver {
    const char *name;
    struct bus_type *bus;
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    void *ops;              // execute 函数等
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

    // tool bus - skill (带依赖)
    {"bus":"tool", "name":"pptx",
     "driver":"skill_pptx",
     "dependencies":[{"bus":"mcp","name":"python"}, {"bus":"mcp","name":"terminal"}]},

    // mcp bus
    {"bus":"mcp", "name":"python",   "driver":"mcp_python", "path":"/usr/bin/python3"},
    {"bus":"mcp", "name":"terminal", "driver":"mcp_terminal"},

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
//   {"bus":"mcp", ...}  → mcp_bus.add_device()
//   {"bus":"llm", ...}  → llm_bus.add_device()
```

## 生命周期管理

### 热插拔

```c
// 安装: python 上线 → pptx 自动可用
mcp_bus.add_device(device_python)
  → bus_probe(&mcp_bus, device_python)  ← python MCP 绑定
  → bus_reprobe(&tool_bus, "pptx")      ← 重新 probe pptx
  → probe OK → bind ✅
  → Agent 工具列表自动出现 pptx

// 卸载: python 下线 → pptx 自动消失
mcp_bus.remove_device("python")
  → tool_bus.each_device(depends_on("mcp", "python"))
  → driver.remove() → unbind
  → Agent 工具列表自动移除 pptx
```

### Probe 失败不清理设备

和内核一模一样 —— probe 失败的设备保留在总线上，等依赖满足后自动重新绑定：

```c
// python 不存在时
probe(pptx) → bus_device_exists("mcp", "python") → false
  → return -ENODEV
  → dev->drv = NULL          ← 设备保留，不清理
  → pr_warn("pptx: probe failed, waiting for python MCP")

// python 安装后
mcp_bus.add_device(python)
  → tool_bus.reprobe("pptx")
  → bus_device_exists("mcp", "python") → true ✅
  → probe OK → bind ✅
```

类比：`/dev/sda` 没插盘时驱动 probe 失败，设备节点还在，盘插上自动绑定。

## 初始化流程

```c
// 1. 总线创建
bus_create(&tool_bus);     // Agent 可调用的工具
bus_create(&mcp_bus);      // 底层能力 (不暴露给 Agent)
bus_create(&channel_bus);  // 消息通道
bus_create(&llm_bus);      // LLM 后端

// 2. 驱动注册
driver_register(&tool_files_read_driver, &tool_bus);
driver_register(&skill_pptx_driver, &tool_bus);
driver_register(&mcp_python_driver, &mcp_bus);

// 3. 设备树解析
parse_device_tree("spiffs_data/tools/device.json", &tool_bus);
parse_device_tree("spiffs_data/skills/*/device.json", &tool_bus);
parse_device_tree("spiffs_data/config/config.json", &llm_bus);

// 4. 自动 probe
bus_probe_all(&tool_bus);
// 结果:
//   read_file → bind ✅
//   pptx      → python 可用 → bind ✅
//   pdf       → libreoffice 不可用 → unbind ❌
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
  mcp_bus:     "python 就是 python，不关心谁用它"
  skill_pptx:  "我有依赖 [python]，probe 时检查"

  三层互不感知 —— 改 python 路径不改 pptx 一行代码
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
       ├── bus_init()                 ← 创建 4 条设备总线实例
       │    ├── tool_bus     (catch-all match)
       │    ├── mcp_bus      (name match)
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
    │        mcp_bus               │    │           llm_bus                     │
    │  match: name match           │    │  match: name match                    │
    │  (空，待实现)                 │    │                                      │
    │                              │    │  drivers:                            │
    │  drivers: []                 │    │    openai_compatible                  │
    │  devices: []                 │    │    anthropic_compatible               │
    │                              │    │                                      │
    │  未来:                       │    │  devices: (模型实例，待注册)            │
    │    python ─→ mcp_python      │    │    deepseek-v4 ─→ openai_compatible   │
    │    terminal ─→ mcp_terminal  │    │                                      │
    └──────────────────────────────┘    └──────────────────────────────────────┘
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

### 当前文件结构

```
ipc/
├── bus.h                消息总线（队列型，入站/出站）
├── bus.c                消息总线实现
├── bus_device.c   ✨    设备总线核心（10 个 API 函数）
├── bus_init.c     ✨    4 条总线实例创建
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
