# Daima Agent 工作流程

## 启动流程

```
main()
  ├─ bootstrap_prepare_runtime()     → paths_init + config.json 加载
  ├─ do_basic_setup()                → 4 级 initcall 链
  │   ├─ [core]       message_bus + core_ipc + agent_hooks
  │   ├─ [postcore]   memory_store + session_store
  │   ├─ [subsys]     cron + heartbeat + http_proxy + skill_loader
  │   └─ [device]     bus_init → 创建 tool_bus/channel_bus/llm_bus
  │                   → bus_channel_register_all (feishu/vector/voice/gateway)
  │                   → bus_llm_register_all (openai/anthropic drivers)
  │                   → executor_core_start + memory_core_start
  ├─ llm_proxy_init()               → 读取 config.json 配置 LLM API
  ├─ tool_registry_init()           → 注册 25 个内置工具
  ├─ of_populate_default()          → 加载 device_tree.json
  ├─ agent_loop_init()              → 后台压缩线程 + 学习审阅
  ├─ agent_loop_start()             → Agent 主循环线程
  └─ ws_server_start()              → WebSocket 服务器 (port 1234)
```

## Agent 主循环

```
agent_loop_task() 循环:
  │
  ├─ process_core_reply()           ← 优先处理核间回复
  │   ├─ TASK_LOAD_CONTEXT 回复 → 恢复异步 turn
  │   └─ 工具执行回复 → 注入结果，继续 LLM 循环
  │
  └─ message_bus_pop_inbound()      ← 收到用户消息
      └─ process_new_message_async()
           ├─ agent_self_test()     ← 如果是 !test
           ├─ turn_context_save()   ← 保存快照
           ├─ core_send(LOAD_CONTEXT) → 分发到记忆核
           └─ 切出等待 process_core_reply
```

## Turn 流水线

```
消息入站
  ├─ hooks_trigger_intent()     → 意图分类 (QA/IMPLEMENT/FIX/...)
  ├─ hooks_trigger_prepare()    → plan + role 注入
  ├─ turn_prepare()             → 构建 system prompt + 加载历史
  ├─ hooks_trigger_before_run() → 模型路由选择
  │
  ├─ turn_run()                 → LLM 工具调用循环 (最多 20 轮)
  │   ├─ llm_chat_tools_with_model() → 调 LLM API
  │   ├─ LLM 返回 tool_use → tool_runtime_execute_call()
  │   │   ├─ 输入补丁 (cron/channel 注入)
  │   │   ├─ tool_registry_execute_for_channel()
  │   │   └─ sudo 密码重试
  │   └─ LLM 返回 text → 结束循环
  │
  ├─ turn_finish()              → Ralph Loop 检查 + 持久化
  │   └─ turn_persist()         → 保存会话 + 推送出站
  └─ channel_router             → 分发回复到通道
```

## 工具系统

### 注册流程

```
tool_registry_init()
  ├─ driver_register(&tool_xxx_driver()->drv, tool_bus)  ← 注册驱动
  ├─ register_tool(tool_xxx_definition())                 ← 注册工具定义
  │   └─ 内部调用 device_register(dev, tool_bus)         ← 注册设备
  └─ build_tools_json()                                    ← 生成 LLM 工具 JSON

25 个内置工具:
  weather, get_current_time, files, apply_patch, restore_file,
  todo, work_item, webfetch, log_tool, skills, session_search,
  cron, terminal, delegate_task,
  11 个 robot_* 工具 (Vector 机器人控制)
```

### 工具调用路径

```
LLM 返回 tool_use(call)
  → tool_runtime_execute_call(call, msg, output, size, &result)
    ├─ tool_invocation_context_patch_input()   ← cron 注入 channel/chat_id
    ├─ tool_registry_execute_for_channel()
    │   ├─ channel 权限检查 (robot 工具仅 vector/voice)
    │   └─ tool_registry_execute()
    │       ├─ bus_find_device(tool_bus, name)  → 总线查找
    │       ├─ container_of → struct tool_driver
    │       └─ driver->execute(input, output, size)
    └─ maybe_retry_terminal_with_web_sudo()
```

### 自定义工具

`spiffs_data/config/custom_tools.json`:
```json
{"name": "my_tool", "driver": "terminal_exec", "command": "my_prefix"}
```
- `driver` 复用已有工具驱动
- `command` 前缀追加到 LLM 输入

## Skill 系统

### 三层模型

```
skill_module (容器)
  ├─ probe()   → 检查依赖工具是否存在 (bus_device_exists)
  ├─ load()    → 解析 TOOLS.json → 注册到 tool_bus
  └─ unload()  → 注销动态工具
```

### Skill 加载

```
skill_loader_init()
  └─ 扫描 spiffs_data/skills/ 目录
     └─ 解析 SKILL.md (YAML front matter + # 标题)
        └─ skill_tools_register()
           └─ 读取 TOOLS.json
              └─ tool_registry_register_dynamic()
                 └─ 注册到 tool_bus
```

### Skill 工作原理

1. **技能声明**: `skills/<name>/SKILL.md` 包含 YAML 元数据和指令
2. **工具声明**: `skills/<name>/TOOLS.json` 定义技能提供的工具
3. **运行时**: LLM 通过 `skills` 工具浏览技能列表，通过 `tool_bus` 调用技能工具

### 工具可见性

```
tool_registry_get_tools_json_for_channel(channel)
  ├─ 非 vector/voice 通道 → s_base_tools_json (不含 robot_* 工具)
  └─ vector/voice 通道    → s_tools_json (全部工具)
```

## 扩展系统

8 个 LKM 风格扩展，钩子链执行：

```
hooks_trigger_intent(msg)
  → module_intent:  LLM 意图分类
  → module_plan:    生成执行计划
  → module_roles:   角色映射

hooks_trigger_prepare(msg, prompt, messages)
  → module_plan:    注入计划到 prompt
  → module_roles:   注入角色 prompt

hooks_trigger_before_run(msg, &model_override, tools)
  → module_router:  模型路由选择

hooks_trigger_replace_run(msg, prompt, messages, tools, &final_text)
  → module_sched:    多 Agent 调度 (默认关闭)
  → module_interview: Prometheus 访谈模式
  → module_team:     Team Mode

hooks_trigger_finish(msg, response)
  → module_ralph:    Ralph Loop 检查
```

## 多核 IPC

```
CORE_SCHEDULER(0) → Agent 主循环
CORE_MEMORY(1)    → 会话存储、上下文压缩、技能预加载
CORE_EXECUTOR(2)  → 工具执行

通信: core_send(core_id, &task) → queue(32深度) → core_recv → core_reply
```

## 设备树

`spiffs_data/config/device_tree.json` 可选，用于动态注册 LLM 设备：

```json
{"devices": [
  {"bus":"llm_bus", "name":"deepseek_anthropic",
   "data":{"protocol":"anthropic","health_url":"https://api.deepseek.com/anthropic/v1/models"}}
]}
```

`bus_llm.c` 的 probe 会读取 `data.health_url` 做 HTTP GET 连通性检查。

## 速查

| 操作 | 命令 |
|------|------|
| 构建 | `make` |
| 测试 | `make test` |
| 运行 | `AGENT_HOME=~/.agent-data ./build-kbuild/agent` |
| 自检 | Web UI 发 `!test` |
| 代码检查 | `perl scripts/checkpatch.pl --no-tree --strict <file.c>` |
