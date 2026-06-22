# PROJECT KNOWLEDGE BASE

**Generated:** 2026-06-19
**Commit:** c05cf1d
**Branch:** main

## OVERVIEW

Daima Agent — 嵌入式 AI Agent，C11 + Kbuild，单二进制。Linux 内核 1:1 目录映射。

## STRUCTURE

```
./
├── init/main.c                  # 唯一 C 入口 main()
├── kernel/                      # 核心子系统（97 文件，3 子目录）
│   ├── loop.c/hooks.c/intent.c/plan.c/roles.c/router.c  # Turn 流水线
│   ├── turn_{prepare,run,exec,finish,persist,dispatch,context,common}.c
│   ├── context_{build,compress,ops}.c  # 上下文管理
│   ├── channel_{policy,router,runtime}.c  # 通道策略/路由/分发
│   ├── tool_{feedback,guard,notify,exec_fail}.c + auto_verify.c  # 工具反馈
│   ├── executor_core.c/memory_core.c/compaction.c  # 多核执行器
│   ├── interview.c/learning.c/team.c/ralph.c/todo.c/recovery.c/rules.c  # 功能模块
│   ├── work_item.c/workqueue.c/debug.c/self_test.c/cancel.c/state.c  # 工具类
│   ├── sched/                   # 多 Agent 调度（PLANNER/EXECUTOR/REVIEWER）
│   ├── time/                    # Cron 定时任务
│   └── printk/                  # 内核风格日志
├── drivers/                     # 驱动层（10 子目录）
│   ├── tool/                    # 34 工具驱动（见 drivers/tool/AGENTS.md）
│   ├── llm/                     # LLM 协议（OpenAI/Anthropic，见 drivers/llm/AGENTS.md）
│   ├── channel/                 # 4 通道（见 drivers/channel/AGENTS.md）
│   │   ├── feishu/              # 飞书
│   │   ├── gateway/             # WebSocket 网关
│   │   ├── vector/              # Vector/MCP 机器人
│   │   └── voice/ (在 drivers/voice/)
│   ├── memory/                  # 会话/内存存储（见 drivers/memory/AGENTS.md）
│   ├── skill/                   # 技能容器模型（见 drivers/skill/AGENTS.md）
│   └── voice/vision/platform/pet/audio/
├── ipc/                         # 总线模型 + 消息队列 + 核间 IPC（见 ipc/AGENTS.md）
├── net/                         # HTTP (libcurl) + TLS (OpenSSL) + Proxy
├── fs/                          # 路径解析 + 目录创建
├── lib/                         # 工具库（cJSON/base64/env/text/json_helpers/log）
├── include/linux/               # 内核风格头文件（17 文件，见 include/linux/AGENTS.md）
├── arch/{host,mips,arm}/        # 平台抽象（见 arch/AGENTS.md），arm/ 仅含 Makefile 存根
├── extensions/                  # LKM 风格模块（见 extensions/AGENTS.md）
├── scripts/                     # Kbuild 引擎（6 文件：Makefile.build, Kbuild.include 等）
├── spiffs_data/                 # 运行时数据（config/skills/web/ca/cron.json/pets）
├── docs/                        # ARCHITECTURE.md
├── packaging/deb/               # Debian 打包脚本
├── .clang-format                # C 代码风格（25 行，替代 checkpatch.pl）
└── Makefile                     # Kbuild 顶层
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 启动流程 | `init/main.c` → `init/bootstrap.c` | main() → bootstrap → 4 级手动 initcall 链 + extensions_init() |
| Agent 主循环 | `kernel/loop.c` | 消息弹出 → intent → plan → turn 流水线 |
| LLM 调用 | `drivers/llm/llm_openai_payload.c` | OpenAI/Anthropic 协议 |
| 工具执行 | `drivers/tool/` + `kernel/turn_exec.c` | 34 工具，见 `drivers/tool/AGENTS.md` |
| 总线模型 | `include/linux/bus.h` + `ipc/bus_device.c` | bus_type/device/driver + probe() |
| 调度器 | `kernel/sched/core.c` | runqueue + agent 生命周期 |
| 锁机制 | `include/linux/mutex.h` `spinlock.h` | pthread 内联宏封装 |
| 构建系统 | `scripts/Makefile.build` + `scripts/Kbuild.include` | obj-y 递归编译 |
| 代码风格 | `.clang-format` | 100 字符行宽，tab 缩进（`checkpatch.pl` 作为备选） |
| 测试 | `kernel/self_test.c` | `!test` 消息触发集成自检 |
| 运行时配置 | `spiffs_data/config/config.json` | LLM keys, ports, channels |
| 运行时自检 | `kernel/self_test.c` | 发 `!test` 消息触发 10 集成测试 |

## CODE MAP

| Symbol | Type | Location | Role |
|--------|------|----------|------|
| `main()` | Function | `init/main.c:57` | 唯一入口，4 阶段启动 |
| `bootstrap_prepare_runtime()` | Function | `init/bootstrap.c:54` | 路径初始化 + 配置加载 |
| `do_basic_setup()` | Function | `init/bootstrap.c:112` | 手动 4 级 initcall 链 |
| `agent_loop_task()` | Function | `kernel/loop.c:377` | Agent 主事件循环 |
| `agent_loop_start()` | Function | `kernel/loop.c:417` | 创建 agent_loop_task 线程 |
| `sched_dispatch()` | Function | `kernel/sched/core.c` | 按 intent 调度 agent |
| `bus_type` | Struct | `include/linux/bus.h` | 总线抽象（match+probe） |
| `struct driver` | Struct | `include/linux/driver.h` | 驱动（probe/remove） |
| `struct device` | Struct | `include/linux/bus.h` | 设备（deps/binding） |
| `struct core_task` | Struct | `include/linux/core_task.h` | 核间任务协议 |
| `struct tool_driver` | Struct | `drivers/tool/tool_types.h` | 工具驱动（嵌入式 struct driver） |
| `struct sched_runqueue` | Struct | `kernel/sched/sched.h` | Agent 运行队列 |
| `struct mutex` | Struct | `include/linux/mutex.h` | 内核互斥锁 |
| `struct spinlock` | Struct | `include/linux/spinlock.h` | 内核自旋锁 |
| `core_send/recv/reply` | Functions | `include/linux/core_task.h` | 3 核 IPC（SCHEDULER/MEMORY/EXECUTOR） |
| `container_of()` | Macro | `include/linux/kernel.h` | 内核内省（成员→父结构体） |
| `ARRAY_SIZE()` | Macro | `include/linux/kernel.h` | 编译期数组大小 |

## CONVENTIONS

### 代码风格（.clang-format 优先）
- **缩进：tab，8 空格宽**
- **行宽：100 字符**
- **函数大括号：下一行**（K&R 风格：`int foo()\n{`）
- **控制流/结构体大括号：同行**（`if (x) {`, `struct foo {`）
- **C99 `//` 注释：允许**（区别于 Linux 内核）
- **无行尾空格**
- **`-Wall -Wextra -std=gnu11`** 全部编译生效
- 备选检查：`perl scripts/checkpatch.pl --no-tree --strict <file.c>`

### 构建系统（Kbuild）
- 每目录声明 `obj-y := file1.o file2.o`
- 子目录递归：`obj-y += subdir/`（末尾 `/` 必须）
- 编译产物：`build-kbuild/agent`（单二进制）
- 详细模式：`make V=1` / `make V=2`（原因诊断）
- 架构选择：`ARCH=host`（默认）/ `ARCH=mips` / `ARCH=arm`
- cJSON 独立编译（`lib/cjson.c`，3119 行第三方库）

### 初始化（手动 4 级 initcall 链，非 section 迭代）
`do_basic_setup()` 在 `init/bootstrap.c:112` 手动按序调用：
- **core(1)**：`message_bus_init` → `core_ipc_init` → `agent_hooks_init`
- **postcore(2)**：`memory_store_init` → `session_store_init`
- **subsys(4)**：`cron_service_init` → `heartbeat_init` → `http_proxy_init` → `skill_loader_init`
- **device(6)**：`bus_init` → `bus_channel_register_all` → `bus_llm_register_all` → `executor_core_start` → `memory_core_start`
- **extensions**：`extensions_init()` 在 device(6) 后调用，按序初始化 8 个扩展模块

定义的 4 级实际使用；原 8 级 initcall section 机制已移除。`module_init()`/`device_initcall()` 现为空宏。`extensions_init()` 在 `extensions/ext_init.c` 中显式调用 8 模块的 `__init` 函数。

### 驱动模型
- 3 条总线：`tool_bus`, `channel_bus`, `llm_bus`
- 匹配：默认 `strcmp(dev->name, drv->name)`
- probe 失败：设备留在总线上，等待依赖上线后 `bus_reprobe()`
- 技能模型：`skill_module` 三层（容器→设备→驱动），见 `drivers/skill/AGENTS.md`
- `struct driver` 必须是嵌入结构体的首字段（`container_of` 要求）

### 锁机制
- `include/linux/mutex.h` `spinlock.h` — 内联宏封装（pthread_mutex/pthread_spin 直接调用），无 .c 实现文件

### 内存管理
- 所有动态分配走 `kmalloc(size, GFP_KERNEL)` / `kfree()`（封装 malloc/free）
- cJSON：调用者用 `cJSON_Delete()` 释放解析结果，用 `free()` 释放打印结果
- `container_of()` 用于链表→结构体遍历

## ANTI-PATTERNS (THIS PROJECT)

- **PLANNER 永不可写代码**：`kernel/sched/class.c` 调度类约束
- **子 agent 不可递归委托**：`drivers/tool/tool_delegate.c` 禁止调用 `delegate_task`
- **Skill ≠ Driver**：Skill 是容器，不是驱动（见 `docs/ARCHITECTURE.md`）
- **不可直写 `cJSON.valueint`**：用 `cJSON_SetNumberValue()`（DEPRECATED API）
- **不可发半成品消息**：`spiffs_data/config/SOUL.md` 行为边界
- **Plan 不可含 TODO/TBD**：`kernel/plan.c` 会拒绝并重生成

### 文档生成技能 NEVER 规则
- PPTX：不可用 unicode 项目符号（•）、`#` 前缀 hex 颜色、复用 option 对象
- DOCX：不可用 `WidthType.PERCENTAGE`（不兼容 Google Docs）、不可用表格当分隔线
- PDF：不可用 Unicode 上下标（渲染为黑块）
- PPTX/DOCX：不可用 `\n`，PPTX 标题下不可用强调线（AI slop）

## COMMANDS

```bash
make                     # → build-kbuild/agent
make V=1                 # 详细输出
make V=2                 # 详细输出 + 重编译原因诊断
make mips|arm            # 交叉编译
make clean|mrproper      # 清理
./run.sh                 # clean + build + run
./build-kbuild/agent     # 运行
./build-kbuild/agent     # 运行（发 !test 触发自检）
perl scripts/checkpatch.pl --no-tree --strict <file.c>  # 备选风格检查
```

## NOTES

- **cJSON FIXME：** `cJSON_GetArraySize` 可能溢出（`int`←`size_t`），已知且不可修复
- **cJSON TODO：** `cJSON_Compare` O(n²) — 临时实现
- **`$(TOPDIR)/` 目录：** 构建副作用，应删除
- **已提交 .o 文件：** `.gitignore` 已忽略但物理存在于源码树中 — `make clean` 清理
- **Ralph Loop：** 回合结束时有未完成 TODO 会强制追加警告（`extensions/module_ralph.c`）
- **中文注释：** 项目约定使用中文文档注释（如 `loop.h`："智能体主循环接口"）
- **无 Docker/容器支持：** 项目不含 Dockerfile，构建依赖本地工具链
- **`spiffs_data/config/AGENTS.md`：** 运行时 agent 指令文件，非知识库文档
- **initcall 实现：** 手动调用链（`do_basic_setup()`），非 Linux 内核 section 迭代
