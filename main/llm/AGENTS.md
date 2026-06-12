# LLM 子系统

**模块**: `main/llm/`
**职责**: 大模型代理、HTTP 客户端、协议适配（OpenAI/Anthropic）

## STRUCTURE

```
main/llm/
├── llm_proxy_host.c/h          # LLM 代理主逻辑（流式处理）
├── llm_http_client_host.c/h    # HTTP 客户端（curl 封装）
├── llm_openai_payload.c/h      # OpenAI 协议 payload 构建/解析
└── llm_anthropic_payload.c/h   # Anthropic 协议 payload 构建/解析
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| LLM 请求入口 | `llm_proxy_host.c` | `llm_proxy_init()` → 流式请求 |
| HTTP 传输 | `llm_http_client_host.c` | curl + TLS 封装 |
| OpenAI 协议 | `llm_openai_payload.c` | payload 构建、分片解析 |
| Anthropic 协议 | `llm_anthropic_payload.c` | payload 构建、分片解析 |

## CONVENTIONS

- 固定 OpenAI 兼容协议，默认 DeepSeek v4-pro
- 流式响应缓存：`DAIMA_LLM_STREAM_BUF_SIZE` = 256KB
- Payload 日志：`DAIMA_LLM_LOG_VERBOSE_PAYLOAD` 控制完整输出
- 支持 thinking/reasoning 内容解析

## ANTI-PATTERNS

- 不要在 LLM 层处理业务逻辑（只负责请求/响应）
- 不要修改协议解析影响工具调用格式
- 不要阻塞在 HTTP 请求（使用异步/流式）
