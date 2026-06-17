# Agent Self-Test (`--test`) 文档

`./build-kbuild/agent --test` 运行 10 个自动化测试，覆盖多核架构、消息总线、LLM 完整链路、异步压缩调度。

## 使用

```bash
./build-kbuild/agent --test
```

初始化完成后运行自检，输出结果并退出。不进入事件循环，不接收外部消息。

## 测试流程

```
main.c
  └─ --test 分支
       ├─ bootstrap_prepare_runtime()
       ├─ do_basic_setup()        ← 消息总线、三核队列、通道/LLM 驱动
       ├─ llm_proxy_init()        ← LLM 配置就绪
       ├─ tool_registry_init()    ← 25 个工具全部注册
       ├─ agent_loop_init()
       └─ agent_self_test()       ← 运行 7 个测试
            ├─ 组件测试 (1-5)     内核组件独立验证
            └─ 集成测试 (6-7)     端到端链路
```

## 测试详情

| # | 测试 | 验证什么 | 依赖 |
|---|------|---------|------|
| 1 | executor queue + tool execution | 执行核接收任务 → 执行 `get_current_time` → 回复有效时间 | core_ipc, tool_bus |
| 2 | message_bus push/pop | 入站队列 push → pop 消息内容一致 | message_bus |
| 3 | tool_bus 6 key tools bound | weather/terminal/files/todo/webfetch/get_current_time 全部绑定 | tool_bus, driver |
| 4 | memory queue + load nonexistent | 记忆核加载不存在会话 → 回复 `failed` | core_ipc, memory_core |
| 5 | real tool via executor core | `get_current_time` 经执行核执行 → 返回有效时间字符串 | core_ipc, executor_core |
| 6 | message pipeline inbound→outbound | push outbound → pop 通道信息一致 | message_bus |
| 7 | LLM end-to-end pipeline | agent_process_message("reply OK") → LLM API 请求 → 回复验证 | LLM API key |
| 8 | async compress dispatch | TASK_COMPRESS_CONTEXT → 记忆核 → context_compressor_schedule_if_needed | memory_core |
| 9 | subagent dispatch | IMPLEMENT intent → sched_dispatch → 3 agent 槽位 | sched |
| 10 | delegate_task real execution | delegate_task_execute 真实调 3 subagent LLM | LLM API |

### 测试 7 详细说明

1. 构造 websocket 消息 `chat_id=self_test_llm, content="reply OK"`
2. 同步调用 `agent_process_message()`，走完整的 turn 流水线：intent→plan→role→LLM API→turn_finish
3. 从出站队列检查回复，验证非空
4. LLM 超时或故障 → 测试失败

**前提条件**：`config.json` 中必须配置有效的 LLM API key 和 provider。

## 输出示例

```
========================================
  Agent Self-Test — Multi-Core Check
========================================

17:21:41 [TEST] ✅ PASS: executor queue + tool execution
17:21:41 [TEST] ✅ PASS: message_bus push/pop
17:21:41 [TEST] ✅ PASS: tool_bus 6 key tools bound
17:21:41 [TEST] ✅ PASS: memory queue + load nonexistent
17:21:41 [TEST] ✅ PASS: real tool via executor core
17:21:41 [TEST] ✅ PASS: message pipeline inbound→outbound
117:29:51 [TEST] ✅ PASS: LLM end-to-end pipeline
  LLM response: OK...
17:29:51 [TEST] ✅ PASS: async compress dispatch
----------------------------------------
  Results: 9/10 passed
========================================
```

## 文件位置

| 文件 | 职责 |
|------|------|
| `kernel/self_test.c` | 全部测试用例 |
| `init/main.c` | `--test` 入口和调用路径 |
| `kernel/loop.c` | `agent_process_message()` 同步桥接 |

## 添加新测试

在 `kernel/self_test.c` 中：

```c
/* 测 N: 描述 */
static void test_xxx(void)
{
    // 测试逻辑，用 report("测试名", ok) 输出结果
}

// 在 agent_self_test() 中调用
test_xxx();
```
