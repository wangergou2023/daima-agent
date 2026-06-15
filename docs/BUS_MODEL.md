# daima-agent: Bus/Driver/Device 总线模型

## 创新

将 Linux 内核的 bus/driver/device 模型移植到 AI Agent 架构。

**市面唯一**: 没有其他 AI Agent 框架使用总线模型进行组件解耦。

## 核心概念

| 内核 | daima | 含义 |
|------|-------|------|
| `bus_type` | `bus_type` | 匹配规则 + 注册表 |
| `device` | `device` | 能力需求 ("我需要 XXX") |
| `driver` | `driver` | 能力实现 ("我能做 XXX") |
| `probe()` | `probe()` | 检查需求是否满足 → 绑定 |
| `device tree` | `spiffs_data/` | JSON 文件描述设备资源 |
| `compatible` | `compatible` | 匹配字符串 |
| `resource` | `resource` | 设备需要的资源 (MCP/工具) |

## 五条总线

```
┌─ tool_bus ─────────────────────────────────┐
│ device: {name:"files", schema:{...}}        │
│ driver: probe(name=="files") → bind         │
│ match:  tool名称匹配 (类似 platform_bus)     │
└─────────────────────────────────────────────┘

┌─ skill_bus ────────────────────────────────┐
│ device: {compatible:"daima,pptx",           │
│          requires:["python"]}               │
│ driver: probe(has_python()) → bind          │
│ match:  能力匹配 (类似 pci_bus)              │
└─────────────────────────────────────────────┘

┌─ mcp_bus ──────────────────────────────────┐
│ device: {type:"python", path:"/usr/bin/py"} │
│ driver: probe(type=="python") → bind        │
│ match:  类型匹配 (类似 usb_bus)              │
└─────────────────────────────────────────────┘

┌─ channel_bus ──────────────────────────────┐
│ device: {name:"feishu", app_id:"xxx"}       │
│ driver: probe(protocol=="feishu") → bind    │
│ match:  协议匹配                             │
└─────────────────────────────────────────────┘

┌─ llm_bus ──────────────────────────────────┐
│ device: {model:"deepseek-v4", url:"http://" │
│         10.3.20.46:4000"}                   │
│ driver: probe(model_match) → bind           │
│ match:  模型+端点匹配                        │
└─────────────────────────────────────────────┘
```

## 数据结构

```c
// bus.h - 总线抽象层
struct bus_type {
    const char *name;
    int (*match)(struct device *dev, struct driver *drv);
    int (*register_device)(struct bus_type *bus, struct device *dev);
    int (*register_driver)(struct bus_type *bus, struct driver *drv);
    struct list_head devices;
    struct list_head drivers;
};

// driver.h - 驱动
struct driver {
    const char *name;
    const char *bus_name;
    const char **compatible;      // ["daima,tool-read-file", NULL]
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    struct list_head node;
};

// device.h - 设备 (能力需求)
struct device {
    const char *name;
    const char *bus_name;
    const char *compatible;
    struct resource *resources;   // 设备需要的资源列表
    int resource_count;
    void *data;                   // driver私有数据
    struct driver *drv;            // 绑定的驱动
    struct list_head node;
};

// resource.h - 资源
struct resource {
    const char *bus;              // "mcp_bus" / "tool_bus"
    const char *name;             // "python" / "terminal"
};
```

## 初始化流程

```c
// init/main.c
static void __init daima_init(void) {
    // 1. 创建总线
    bus_create(&tool_bus);
    bus_create(&skill_bus);
    bus_create(&mcp_bus);
    bus_create(&channel_bus);
    bus_create(&llm_bus);
    
    // 2. 注册驱动 (编译时静态注册, 类似 module_platform_driver)
    tool_register_drivers();
    skill_register_drivers();
    mcp_register_drivers();
    
    // 3. 解析设备树 (spiffs_data/)
    parse_device_tree("spiffs_data/tools.json", &tool_bus);
    parse_device_tree("spiffs_data/skills/", &skill_bus);
    parse_device_tree("spiffs_data/config/config.json", &llm_bus);
    
    // 4. 自动 probe (总线匹配 device → driver → bind)
    bus_probe_all(&tool_bus);
    bus_probe_all(&skill_bus);
    bus_probe_all(&mcp_bus);
}
```

## 设备树格式 (spiffs_data/)

```json
// tools.json - tool device tree
{
  "devices": [
    {"compatible": "daima,tool-read-file", "schema": "...", "hidden_on": []},
    {"compatible": "daima,tool-terminal",  "schema": "...", "hidden_on": ["feishu"]}
  ]
}

// skills/pptx/device.json - skill device tree
{
  "compatible": "daima,skill-pptx",
  "description": "Generate PowerPoint files",
  "requires": [
    {"bus": "mcp_bus", "name": "python"},
    {"bus": "mcp_bus", "name": "terminal"}
  ]
}

// config.json providers - LLM device tree
{
  "active_provider": "ingenic_local_deepseek",
  "providers": {
    "ingenic_local_deepseek": {
      "compatible": "daima,llm-openai-compatible",
      "model": "deepseek-v4-pro",
      "openai_base_url": "http://10.3.20.46:4000"
    }
  }
}
```

## Probe 流程

```
device tree 解析
    ↓
bus.probe_all()
    ↓
for each device:
  for each driver on same bus:
    if bus->match(device, driver):
      driver->probe(device)
        ↓
      driver检查device的resources是否满足
        ├── 满足 → bind (dev->drv = drv) → 返回 OK
        └── 不满足 → -ENODEV (unbind)
```

## 为什么是创新

1. **无人做过**: AI Agent 领域没有 bus/driver/device 模型
2. **内核验证**: 这个模型在 Linux 内核里支撑了 30 年，处理了 5000+ 驱动
3. **自然映射**: AI Agent 的"能力需求→能力实现"就是 device→driver 的 probe 过程
4. **热插拔**: 新 skill/MCP 进来 → 自动 probe → 自动可用，不需要重启

## 当前状态

- [x] `strcut bus_type` 设计
- [x] `struct device` + `struct driver` 设计
- [x] `struct resource` 设计
- [x] 五条总线定义 (tool/skill/mcp/channel/llm)
- [x] 设备树格式
- [x] probe 流程
- [ ] 代码实现
- [ ] 现有 tool_registry → tool_bus 迁移
- [ ] 现有 skill_loader → skill_bus 迁移
