# Channels 子系统

**模块**: `main/channels/`
**职责**: 消息通道适配（飞书、Vector 机器人）

## STRUCTURE

```
main/channels/
├── feishu/                     # 飞书/Lark 通道
│   ├── feishu_bot.c/h          # Bot 启动与 WS 管理
│   ├── feishu_ws_transport.c/h # WebSocket 传输层
│   ├── feishu_ws_runtime.c/h   # WS 运行时
│   ├── feishu_ws_proto.c/h     # WS 协议解析
│   ├── feishu_event_handler.c  # 事件处理
│   ├── feishu_message.c        # 消息构造
│   ├── feishu_media.c          # 媒体上传
│   ├── feishu_api.c            # HTTP API 调用
│   └── feishu_targets.c        # 目标用户/群管理
└── vector/                     # Vector 机器人通道
    ├── vector_channel.c/h      # 通道主逻辑
    └── mcp_client.c/h          # MCP 客户端（robot-mcp 子进程）
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 飞书 Bot 启动 | `feishu/feishu_bot.c` | `feishu_bot_init()` → `feishu_bot_start()` |
| 飞书 WS 连接 | `feishu/feishu_ws_transport.c` | 传输层，重连逻辑 |
| 飞书事件处理 | `feishu/feishu_event_handler.c` | 消息/事件分发 |
| Vector 通道 | `vector/vector_channel.c` | 音频缓冲、ASR 交接 |
| MCP 客户端 | `vector/mcp_client.c` | popen robot-mcp，JSON-RPC |

## CONVENTIONS

- 通道通过 `channel_router.c` 统一路由
- 飞书使用 WebSocket 长连接，自动重连
- Vector 使用 MCP 协议与 robot-mcp 通信
- MCP 超时：`MCP_INIT_TIMEOUT_MS` / `MCP_CALL_TIMEOUT_MS` = 30s

## ANTI-PATTERNS

- 不要在通道层处理业务逻辑（交给 agent loop）
- 不要绕过 channel_router 直接发送消息
- MCP 客户端不要阻塞主线程
