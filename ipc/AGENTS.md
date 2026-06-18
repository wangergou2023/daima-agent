# IPC KNOWLEDGE BASE

**Updated:** 2026-06-19

## OVERVIEW

总线模型 + 消息队列 + 核间 IPC。3 条总线（tool/channel/llm），device/driver matching，core_send/recv 3 核通信。每条总线维护 devices/drivers 双链表，注册时自动 probe 匹配。

## STRUCTURE

```
ipc/
├── bus.h              # struct message + 通道常量 (CHAN_FEISHU/WEBSOCKET/VECTOR...)
├── bus.c              # 消息总线：入/出队列（push/pop，阻塞消费）
├── bus_init.c         # 创建 3 条总线实例 (tool_bus/channel_bus/llm_bus)
├── bus_device.c       # bus_type/device/driver 生命周期（核心 283 行）
├── bus_channel.c      # 通道总线注册（feishu/vector/voice/gateway 4 条）
├── bus_llm.c          # LLM 总线注册（驱动 + 4 设备直注册，含 health check）
├── core_ipc.c         # 3 核消息队列（queue depth 32，core_reply 回调度核）
└── Makefile           # obj-y := bus.o bus_device.o ...
```
## WHERE TO LOOK

| Task | File | Key Function |
|------|------|-------------|
| 总线初始化 | `bus_init.c` | `bus_init()` → bus_create × 3 |
| 设备树加载 | `bus_llm.c` | `bus_llm_register_all()` 直接注册 4 个 LLM 设备（不再通过 JSON） |
| 通道注册 | `bus_channel.c` | `bus_channel_register_all()` → driver_register + device_register |
| LLM 注册 | `bus_llm.c` | `bus_llm_register_all()` → driver_register + 4 设备直注册 + health check |
| 核间通信 | `core_ipc.c` | `core_send/recv/reply` — 3 核独立队列 |
| 消息入/出站 | `bus.c` | `message_bus_push/pop_inbound/outbound` |
| 驱动匹配 | `bus_device.c` | `bus_probe()` → default_name_match (strcmp) |
| 依赖重试 | `bus_device.c` | `bus_reprobe()` — 依赖就绪后重新绑定 |

## CONVENTIONS

- **bus_type.match 默认 strcmp**：未提供自定义 match 时退化按名称匹配（大小写敏感）
- **probe 失败设备留总线**：设备不删除，等待 `bus_reprobe()` 重试（用于依赖未就绪场景）
- **core_task 3 核**：0=SCHEDULER, 1=MEMORY, 2=EXECUTOR；`core_reply()` 一律回发调度核
- **消息所有权转移**：push_inbound 接管 content/image_path；push_outbound 接管 content/reasoning
- **结构化嵌入**：`struct driver` 必须是嵌入结构体的首字段（`container_of` 从链表节点还原）

## KEY SYMBOLS

| Symbol | Location | Role |
|--------|----------|------|
| `struct message` | `bus.h:24` | 总线消息：channel/chat_id/source/content/intent |
| `struct bus_type` | `include/linux/bus.h` | 总线抽象：name + match + devices/drivers 链表 |
| `bus_init()` | `bus_init.c:9` | 创建 3 总线实例 |
| `message_bus_init()` | `bus.c:11` | 入站+出站队列初始化 |
| `bus_channel_register_all()` | `bus_channel.c:58` | 4 通道驱动+设备批量注册 |
| `bus_llm_register_all()` | `bus_llm.c:61` | 2 LLM 驱动注册 + 4 设备直接注册 |
| `core_ipc_init()` | `core_ipc.c:14` | 3 核消息队列初始化 |
| `bus_reprobe()` | `bus_device.c:245` | 按名称重新 probe 未绑定设备 |
| `device_register()` | `bus_device.c:176` | 设备注册 + 立即触发 probe |
| `driver_register()` | `bus_device.c:124` | 驱动注册（不立即 probe） |
| `container_of()` | `include/linux/kernel.h` | 链表节点→父结构体（内核内省宏） |
