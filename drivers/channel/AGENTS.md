# CHANNEL KNOWLEDGE BASE

**Generated:** 2026-06-19
**Commit:** 0d927af
**Branch:** main

## OVERVIEW

3 通道实现，注册在 `channel_bus`，共 31 文件（C + H + Makefile）。

```
drivers/channel/
├── feishu/        飞书通道（20 文件）— WebSocket 长连接 + Open API
├── gateway/       WebSocket 网关（6 文件）— 原生 WS 服务 + HTTP 路由 + 前端资源
├── vector/        Vector/MCP 机器人（5 文件）— JSON-RPC 2.0 + 音频流
├── Kconfig        功能开关 (FEISHU_ENABLED / VECTOR_ENABLED / GATEWAY_ENABLED)
└── Makefile       obj-y := feishu/ vector/ gateway/
```

## STRUCTURE

| 目录 | 文件数 | 描述 |
|------|--------|------|
| `feishu/` | 20 | 飞书 IM 通道：WebSocket 传输层 (TLS/帧/握手)、事件处理器 (去重/消息解析/入站推送)、HTTP API (libcurl)、媒体下载、会话路由 |
| `gateway/` | 6 | WebSocket 网关：`ws_server.h` (接口)、`ws_client.c` (客户端会话表/keepalive/消息分发)、`ws_http_helpers.c` (HTTP 路由/静态资源/API) |
| `vector/` | 5 | Vector 机器人：`vector_channel.c` (会话管理/音频缓冲/ASR 管道)、`mcp_client.c` (fork+exec robot-mcp / JSON-RPC 2.0 / 工具调用) |

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 飞书事件处理 | `feishu/feishu_event_handler.c` | `im.message.receive_v1` 解析 → 去重 → 文本/图片/富文本 → `message_bus_push_inbound()` |
| 飞书 WS 传输 | `feishu/feishu_ws_transport.c` | TCP+TLS 连接、WebSocket 握手 (Key/Accept)、掩码帧收发 |
| 网关 WS 协议 | `gateway/ws_client.c` | 客户端会话表 (WS_MAX_CLIENTS)、`ws_read_frame_header/ws_read_frame_payload`、JSON 消息分发 (message/upload/stop/pet/sudo) |
| 网关 API 路由 | `gateway/ws_http_helpers.c` | GET `/`, `/health`, `/api/*`；POST `/api/session_delete`, `/api/terminal_security` |
| 网关接口定义 | `gateway/ws_server.h` | `ws_server_start/send/send_pet_response/send_sudo_request/stop` |
| MCP 集成 | `vector/mcp_client.c` | fork+exec robot-mcp → pipe 双向通信 → JSON-RPC `initialize` → `drivers/tool/call` / `drivers/tool/list` |
| Vector 音频 | `vector/vector_channel.c` | PCM 缓冲 (16kHz/16bit/mono) → VAD 分块 → ASR → `message_bus_push_inbound()` |
| 通道注册 | `ipc/bus_channel.c` | `bus_channel_register_all()` 注册 feishu/vector/voice/gateway 4 驱动 + 设备 |

## CONVENTIONS

- **注册**：所有通道通过 `bus_channel_register_all()` 在 `channel_bus` 上注册 `struct driver` + `struct device`
- **飞书**：WebSocket 长连接 (`wss://`)，libcurl HTTP API，Bearer Token 认证，去重缓存 (FNV-1a, 64 槽)，text/image/post 三种消息类型
- **网关**：原生 TCP WS 服务，同端口复用 HTTP + WS (Upgrade 头检测)，客户端帧帧间必须 MASK，服务器→客户端不 MASK，Ping/Pong keepalive，过期驱逐
- **Vector**：通过 fork+exec+pipe 启动子进程 robot-mcp，MCP JSON-RPC 2.0 协议，`setlinebuf` 保证行缓冲，异步音频通知 (notifications/audio/chunk + done)，base64 PCM 解码
- **消息入站**：3 通道均构造 `struct message` (channel/chat_id/source/content/image_path) → `message_bus_push_inbound()`
- **Kconfig**：`FEISHU_ENABLED` / `VECTOR_ENABLED` / `GATEWAY_ENABLED`，默认 y
