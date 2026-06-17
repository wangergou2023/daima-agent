# Subagent JSON 编码修复方案

## 现象

`delegate_task` 工具调用 subagent 执行 LLM 请求时，API 返回 400：

```
Failed to parse the request body as JSON: system: invalid unicode code point at line 1 column N
```

主 LLM 调用（test 7）正常，同路径、同 cJSON、同 `build_request_body`。subagent 调用稳定失败。

## 排除项

| 检查项 | 结果 |
|--------|------|
| model 字段 | ✅ body 中有 `"model":"deepseek-v4-pro"` |
| 中文 prompt | ✅ 改为纯英文仍然失败 |
| tools JSON | ✅ 传不传 tools 都失败 |
| 换行符 | ✅ agent.c 中已 strip `\n` `\r` |
| proxy 层 | ✅ 换官方 DeepSeek API `api.deepseek.com` 仍失败 |
| `safe_prompt` 缓冲区 | 2048 字节，测试 plan 文本很短，未溢出 |

## 根因假设

**`llm_chat_tools_with_model` 临时修改全局 `s_model`**，在并发或嵌套调用场景下可能：

1. `build_request_body` 依赖全局 `s_use_anthropic_api`，这个值在 `llm_proxy_init` 设置后再不变
2. 但 `s_model` 被 `llm_chat_tools_with_model` 临时修改为 `llm_get_model_name()`
3. 主 LLM 调用也在用 `s_model`，两者交替修改 → 数据竞争

**更可能的根因**：`build_request_body` 内部调用 `cJSON_Parse(tools_json)` 和 `cJSON_PrintUnformatted`。如果 `tools_json`（`s_base_tools_json`）在构建过程中被释放或修改，cJSON 会产生含无效字节的输出。

## 验证方案

### 方案 A：完全独立构建 subagent body，不依赖全局状态

让 `sched_agent_launch` 直接构造 HTTP body 并发送 HTTP 请求，完全绕过 `llm_chat_tools` / `build_request_body` / 全局变量。

改动：`kernel/sched/agent.c` 中直接调 `llm_http_post_json`

### 方案 B：为 subagent 单独缓存 tools_json

在 `sched_agent_launch` 开始处 `strdup(tools)` 并传递副本，避免共享字符串被修改。

改动：`kernel/sched/agent.c`

### 方案 C：固定 s_model 不变，sched_agent_launch 直接调 llm_chat_tools

不使用 `llm_chat_tools_with_model`，而是在调用前用 `llm_set_model` 临时设置，调用后恢复。

改动：`kernel/sched/agent.c`

## 建议执行顺序

1. **先 A**：绕过所有中间层，直连 HTTP。若成功 → 问题在中间层
2. 若 A 成功，B/C 可作为更优雅的永久方案

## 预估

- 方案 A：~30 行代码，15 分钟
- 方案 B：~5 行代码，5 分钟
- 方案 C：~5 行代码，5 分钟
