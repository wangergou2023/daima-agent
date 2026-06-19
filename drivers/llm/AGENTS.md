# drivers/llm/ — LLM 协议层

**6 源文件，2 协议驱动。** 注册在 `llm_bus` 上，4 个 LLM 设备直注册（无 JSON 设备树）。

## OVERVIEW

封装 OpenAI 和 Anthropic 兼容协议的消息构建。工具调用（tool_use）、多模态图片理解、异步非阻塞调用、模型回退策略。初始化时通过 health check 验证设备可达性。

## STRUCTURE

```
drivers/llm/
├── llm_openai_payload.c/h      # OpenAI 兼容协议：messages → JSON payload
├── llm_anthropic_payload.c/h   # Anthropic 兼容协议：messages → JSON payload
├── llm_proxy.h                 # 统一 LLM 调用封装（工具调用/多模态/异步）
├── model_fallback.c/h          # 模型回退策略
└── Makefile                    # obj-y := llm_openai_payload.o llm_anthropic_payload.o model_fallback.o
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 协议注册 | `ipc/bus_llm.c:69-70` | `openai_compatible` + `anthropic_compatible` 2 驱动注册 |
| 设备注册 | `ipc/bus_llm.c:77-81` | 4 设备直注册：deepseek_anthropic / moonshot / bigmodel / ingenic_local_kimi |
| Health check | `ipc/bus_llm.c` (probe) | 驱动 probe 时 HTTP GET `health_url` 验证可达性 |
| 消息构建 | `llm_openai_payload.c` / `llm_anthropic_payload.c` | system prompt + messages + tools → JSON |
| 工具调用响应解析 | `llm_proxy.h:46-61` | `llm_tool_call_t` 结构体，最多 `MAX_TOOL_CALLS` 个调用 |
| 异步调用 | `llm_proxy.h:143-160` | `llm_chat_tools_async()` 非阻塞，`llm_chat_async_is_done()` 轮询 |
| 模型回退 | `model_fallback.c` | 主模型失败时自动切换备用模型 |
| 图片理解 | `llm_proxy.h:70-118` | `#ifdef ENABLE_VISION`，base64 图片编码 + 多模态消息构建 |

## CONVENTIONS

- **2 协议 → 1 接口**：上层通过 `llm_chat_tools()` 统一调用，协议差异由驱动层封装
- **设备直注册**：LLM 设备通过 C 代码内联数组注册（`ipc/bus_llm.c:73-81`），不再走 JSON 设备树
- **Health check**：probe 时 HTTP GET `health_url`，失败则设备保留总线等待重试
- **异步模式**：`llm_async_chat_t` 支持火力开火 + 非阻塞轮询，用于多 Agent 并行 LLM 调用
- **上下文窗口**：`llm_get_context_limit_tokens()` 优先读 `config.json` 的 `context_limit_tokens`，否则回退默认值
- **平台抽象**：`llm_proxy.h` 是 Host/MIPS 共用头文件，具体 HTTP 实现由 `arch/` 平台文件提供（`arch/host/llm_proxy_host.c`）

## KEY STRUCTS

| Struct | File | Role |
|--------|------|------|
| `llm_tool_call_t` | `llm_proxy.h:46-51` | 工具调用：id / name / input（堆分配 JSON） |
| `llm_response_t` | `llm_proxy.h:53-61` | LLM 响应：text + reasoning_content + tool_calls[] |
| `llm_image_content_t` | `llm_proxy.h:76-81` | 多模态图片：base64 数据 + MIME 类型 |
| `llm_async_chat_t` | `llm_proxy.h:143` | 异步 LLM 调用句柄（不透明类型） |
