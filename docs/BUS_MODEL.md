# daima-agent: Bus/Driver/Device 总线模型

## 创新

将 Linux 内核的 bus/driver/device 模型移植到 AI Agent 架构。

**市面唯一**: 没有其他 AI Agent 框架使用总线模型进行组件解耦。

## 核心概念

| 内核 | daima | 含义 |
|------|-------|------|
| `bus_type` | `bus_type` | 匹配规则 + 注册表 |
| `device` | `device` | 能力声明 ("我叫 xxx, 我需要 yyy") |
| `driver` | `driver` | 能力实现 ("我能做 xxx, 我先检查 yyy") |
| `probe()` | `probe()` | 检查依赖是否满足 → 绑定 |
| `device tree` | `spiffs_data/` | JSON 文件描述设备资源 |
| `name match` | `name match` | 字符串精确匹配 |

## 设计原则

**全部字符串匹配。没有语义。Agent 不需要"理解"工具**

Agent 拿到的工具列表和读文件一样:
```
read_file    → tool_bus → name match → probe → execute
write_file   → tool_bus → name match → probe → execute  
pptx         → tool_bus → name match → probe(检查python MCP) → execute
webfetch     → tool_bus → name match → probe → execute
```

## 四条总线

```
┌─ tool_bus ─────────────────────────────────┐
│ 所有可被 Agent 调用的工具                    │
│                                             │
│ 普通工具:                                    │
│   device: {name:"read_file", schema:{}}      │
│   driver: tool_files_read.probe() → execute  │
│   probe: 直接 bind (无依赖)                   │
│                                             │
│ skill工具 (带依赖的工具):                      │
│   device: {name:"pptx",                      │
│            dependencies:[{bus:"mcp",name:"python"}]} │
│   driver: skill_pptx.probe()                 │
│   probe: 检查 mcp_bus 是否有 "python" → bind  │
│                                             │
│ match: 字符串精确匹配 (类似 platform_bus)      │
└─────────────────────────────────────────────┘

┌─ mcp_bus ──────────────────────────────────┐
│ 底层执行能力 (不直接暴露给 Agent)              │
│                                             │
│ device: {name:"python", path:"/usr/bin/py3"} │
│ driver: mcp_python.probe()                  │
│ match: 字符串精确匹配                        │
└─────────────────────────────────────────────┘

┌─ channel_bus ──────────────────────────────┐
│ 消息通道 (不暴露给 Agent)                     │
│                                             │
│ device: {name:"feishu", app_id:"xxx"}        │
│ driver: feishu_channel.probe()               │
│ match: 字符串精确匹配                        │
└─────────────────────────────────────────────┘

┌─ llm_bus ──────────────────────────────────┐
│ LLM 后端 (不暴露给 Agent)                    │
│                                             │
│ device: {model:"deepseek-v4", url:"http://"} │
│ driver: openai_compatible.probe()            │
│ match: model+endpoint 匹配                   │
└─────────────────────────────────────────────┘
```

## 唯一特殊之处: skill 的 probe

```c
// 普通 tool: probe 什么也不检查
static int tool_files_probe(struct device *dev) {
    return 0;  // 直接 bind
}

// skill tool: probe 检查依赖
static int skill_pptx_probe(struct device *dev) {
    if (!bus_device_exists("mcp_bus", "python"))
        return -ENODEV;   // python 不可用 → 不注册这个 tool
    return 0;              // python 可用 → 注册为 Agent 工具
}
```

对 Agent 来说完全透明——python 不可用时, pptx 直接从工具列表消失。

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
    struct driver *drv;
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

## 设备树格式 (spiffs_data/)

```json
// device.json - 所有设备的统一入口
{
  "devices": [
    // tool bus
    {"bus":"tool", "name":"read_file",  "driver":"tool_files_read",  "schema":{...}},
    {"bus":"tool", "name":"write_file", "driver":"tool_files_write", "schema":{...}},
    {"bus":"tool", "name":"terminal",   "driver":"tool_terminal",    "schema":{...}},
    
    // tool bus (skill)
    {"bus":"tool", "name":"pptx",
     "driver":"skill_pptx",
     "description":"生成PPT",
     "schema":{...},
     "dependencies":[{"bus":"mcp","name":"python"}, {"bus":"mcp","name":"terminal"}]},
    
    // mcp bus
    {"bus":"mcp", "name":"python", "driver":"mcp_python", "path":"/usr/bin/python3"},
    {"bus":"mcp", "name":"terminal","driver":"mcp_terminal"},
    
    // llm bus
    {"bus":"llm", "name":"deepseek-v4",
     "driver":"openai_compatible",
     "url":"http://10.3.20.46:4000",
     "model":"deepseek-v4-pro"}
  ]
}
```

**统一解析入口** — 一个函数路由到所有 bus:

```c
// of_populate_all("spiffs_data/device.json")
//   自动路由:
//     {"bus":"tool", ...}   → tool_bus.add_device()
//     {"bus":"mcp", ...}    → mcp_bus.add_device()
//     {"bus":"llm", ...}    → llm_bus.add_device()
```

## 热插拔 + 热移除

```c
// 安装: python 上线 → pptx 自动可用
mcp_bus.add_device(device_python)
  → bus_probe(&mcp_bus, device_python)  ← python MCP 绑定
  → bus_reprobe(&tool_bus, "pptx")      ← 重新 probe pptx
  → probe OK → bind ✅
  → Agent 工具列表自动出现 pptx

// 卸载: python 下线 → pptx 自动消失
mcp_bus.remove_device("python")
  → tool_bus.each_device(depends_on("mcp", "python")):
  → driver.remove() → unbind
  → Agent 工具列表自动移除 pptx
```

## Probe 失败的设备保留在总线上

```c
// probe 失败 → dev->drv = NULL → dev 留在 bus 上
probe(pptx) → bus_device_exists("mcp", "python") → false
  → return -ENODEV
  → dev->drv = NULL    ← 不清理 device
  → pr_warn("pptx: probe failed, waiting for python MCP")

// 以后 python 装上:
mcp_bus.add_device(python)
  → tool_bus.reprobe("pptx")
  → bus_device_exists("mcp", "python") → true ✅
  → probe OK → bind ✅
```

和内核一模一样——`/dev/sda` 没插盘时驱动 probe 失败，设备节点还在，等盘插上自动绑定。

## 初始化流程

```c
// 1. 总线创建
bus_create(&tool_bus);     // Agent 可调用的工具
bus_create(&mcp_bus);      // 底层能力 (不暴露给 Agent)
bus_create(&channel_bus);  // 消息通道
bus_create(&llm_bus);      // LLM 后端

// 2. 驱动注册
driver_register(&tool_files_read_driver, &tool_bus);
driver_register(&skill_pptx_driver, &tool_bus);   // skill 也是 tool bus
driver_register(&mcp_python_driver, &mcp_bus);

// 3. 设备树解析 (创建 device)
parse_device_tree("spiffs_data/tools/device.json", &tool_bus);
parse_device_tree("spiffs_data/skills/*/device.json", &tool_bus);
parse_device_tree("spiffs_data/config/config.json", &llm_bus);

// 4. 自动 probe (match → probe → bind)
bus_probe_all(&tool_bus);
// 结果:
//   read_file → bind ✅
//   pptx      → probe 检查 python MCP → 有 → bind ✅
//   pdf       → probe 检查 libreoffice MCP → 无 → unbind ❌
```

## 为什么是创新

1. **无人做过**: AI Agent 领域没有 bus/driver/device 模型
2. **内核验证**: 这个模型在 Linux 内核里支撑了 30 年
3. **全部字符串匹配**: Agent 不需要理解, bus 精确匹配
4. **skill = tool + dependencies**: 唯一特殊之处是 probe 时多一步检查
5. **热插拔**: 安装 python → pptx 自动可用, 卸载 python → pptx 自动消失
