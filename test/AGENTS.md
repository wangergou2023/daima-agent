# test/ — 测试

**50 测试文件，平铺目录。** 无标准框架，纯 C + `assert.h`。`make test` 运行全部。

## OVERVIEW

每个测试是独立的 C 程序（`test_*.c`），编译为独立可执行文件，通过 `assert()` 断言，成功打印 "passed" 并返回 0。

## WHERE TO LOOK

| Category | Files | Notes |
|----------|-------|-------|
| LLM 测试 | `test_llm_async_chat.c`, `test_llm_http_async.c`, `test_llm_openai_payload.c`, `test_llm_anthropic_payload.c` | 需真实 API |
| 工具测试 | `test_files_tool.c`, `test_skills_tool.c`, `test_terminal.c`, `test_webfetch.c`, `test_cron_tool.c` | 部分需系统依赖 |
| 内核测试 | `test_ralph_loop.c`, `test_intent_gate.c`, `test_plan_review.c`, `test_todo_enforcer.c`, `test_category_router.c` | 纯逻辑 |
| Agent 测试 | `test_agent_coordinator.c`, `test_agent_hooks.c`, `test_agent_roles.c` | Agent 流水线 |
| 会话测试 | `test_session_recovery.c`, `test_session_file_common.c`, `test_session_history_reasoning.c` | 持久化 |
| 上下文测试 | `test_compaction_recovery.c`, `test_context_skills_summary.c` | 压缩/摘要 |
| 安全/编辑 | `test_safe_edit.c`, `test_hashline.c`, `test_apply_patch.c` | 文件安全 |
| 配置测试 | `test_runtime_config.c`, `test_rules_injection.c`, `test_paths.c` | 配置/路径 |
| 其他 | `test_text.c`, `test_pet_event.c`, `test_voice_stub.c` 等 | — |

## CONVENTIONS

### 命名
- **`test_<module>.c`** — 所有测试遵循此模式
- 特例：`test_voice_stub.c` — 存根（非测试），提供 mock 函数

### 断言
- **仅用 `<assert.h>` 的 `assert()`** — 无自定义断言库
- 成功输出：`"<name> tests passed"`
- 返回 0 表示通过，非 0 表示失败

### 隔离
- **临时目录**：`/tmp/agent-<name>-<pid>/`（如 `test_rules_injection.c`）
- **环境变量**：设置 `AGENT_HOME` 为临时路径
- **编译标志控制行为**：
  - `-DAGENT_HOOKS_TEST_RESET=1` — 测试前重置钩子状态
  - `-DINTENT_GATE_LLM_FALLBACK=0` — 禁用 LLM 回退
  - `-DSKILL_SCOPED_TOOLS_ENABLED=0` — 禁用技能工具

### 依赖关联
- 网络/LLM 测试链接真实 `libcurl`, `libssl`, `libcrypto`
- 其他测试只链接被测源文件
- `printk()` 常被 stub 覆盖：`int printk(...) { return 0; }`

### 编译
```makefile
# test/Makefile 每个测试目标模式：
test_xxx: test_xxx.c ../source1.c ../source2.c
	$(CC) $(CFLAGS) -o build/$@ $^ $(TEST_LFLAGS) -D_GNU_SOURCE
```
- 编译标志：`-Wall -Wextra -std=c11 -O0 -g -I.. -I../include`
- 无自动依赖发现 — Makefile 显式列出每个 `../source.c`
