# PROJECT KNOWLEDGE BASE

**Generated:** 2026-06-17
**Commit:** 24cda86
**Branch:** main

## OVERVIEW

Daima Agent — 嵌入式 AI Agent，C11 + Kbuild，单二进制。Linux 内核 1:1 目录映射。

## STRUCTURE

```
./
├── init/main.c                  # 唯一 C 入口 main()
├── kernel/                      # 核心子系统（85 文件）
│   ├── sched/                   # 多 Agent 调度（PLANNER/EXECUTOR/REVIEWER）
│   ├── time/                    # Cron 定时任务
│   ├── printk/                  # 内核风格日志
│   ├── irq/                     # 信号处理存根
│   └── loop/hooks/intent/plan/roles/router/turn_*.c  # Turn 流水线
├── drivers/                     # 驱动层
│   ├── tool/                    # 26+ 工具驱动（见 AGENTS.md）
│   ├── llm/                     # LLM 协议（OpenAI/Anthropic）
│   ├── channel/feishu/          # 飞书通道
│   ├── memory/                  # 会话/内存存储
│   ├── skill/                   # 技能容器模型
│   └── voice/vision/platform/pet/
├── ipc/                         # 总线模型 + 消息队列 + 核间 IPC
├── net/                         # HTTP (libcurl) + TLS (OpenSSL) + Proxy
├── fs/                          # 路径解析 + 目录创建
├── lib/                         # 工具库（cJSON/base64/env/text）
├── include/linux/               # 内核风格头文件（16 文件）
├── include/generated/           # Kconfig 生成 autoconf.h
├── arch/{host,mips,arm}/        # 平台抽象
├── extensions/                  # LKM 风格模块
├── scripts/                     # Kbuild 引擎 + checkpatch.pl + kconfig.py
├── test/                        # 45+ 单元测试（见 AGENTS.md）
├── spiffs_data/                 # 运行时数据（config/skills/web/sessions）
├── Kconfig                      # 功能开关
├── Makefile                     # Kbuild 顶层
└── docs/                        # BUS_MODEL.md, REARCH.md
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 启动流程 | `init/main.c` → `init/bootstrap.c` | main() → bootstrap → 8 级 initcall |
| Agent 主循环 | `kernel/loop.c` | 消息弹出 → intent → plan → turn 流水线 |
| LLM 调用 | `drivers/llm/llm_openai_payload.c` | OpenAI/Anthropic 协议 |
| 工具执行 | `drivers/tool/` + `kernel/turn_exec.c` | 26+ 工具，见 `drivers/tool/AGENTS.md` |
| 总线模型 | `include/linux/bus.h` + `ipc/bus_device.c` | bus_type/device/driver + probe() |
| 调度器 | `kernel/sched/core.c` | runqueue + agent 生命周期 |
| 构建系统 | `scripts/Makefile.build` + `scripts/Kbuild.include` | obj-y 递归编译 |
| 代码风格 | `scripts/checkpatch.pl` | 100 字符行宽，tab 缩进 |
| 测试 | `test/` | `make test`，见 `test/AGENTS.md` |
| 运行时配置 | `spiffs_data/config/config.json` | LLM keys, ports, channels |

## CODE MAP

| Symbol | Type | Location | Role |
|--------|------|----------|------|
| `main()` | Function | `init/main.c:42` | 唯一入口，4 阶段启动 |
| `do_basic_setup()` | Function | `init/bootstrap.c` | 4 级 initcall 链 |
| `agent_loop_task()` | Function | `kernel/loop.c` | Agent 主事件循环 |
| `sched_dispatch()` | Function | `kernel/sched/core.c` | 按 intent 调度 agent |
| `bus_type` | Struct | `include/linux/bus.h` | 总线抽象（match+probe） |
| `struct driver` | Struct | `include/linux/driver.h` | 驱动（probe/remove） |
| `struct device` | Struct | `include/linux/bus.h` | 设备（deps/binding） |
| `struct core_task` | Struct | `include/linux/core_task.h` | 核间任务协议 |
| `struct tool_driver` | Struct | `drivers/tool/tool_registry.h` | 工具驱动（嵌入式 struct driver） |
| `struct sched_runqueue` | Struct | `kernel/sched/sched.h` | Agent 运行队列 |
| `core_send/recv/reply` | Functions | `include/linux/core_task.h` | 3 核 IPC（SCHEDULER/MEMORY/EXECUTOR） |
| `container_of()` | Macro | `include/linux/kernel.h` | 内核内省（成员→父结构体） |
| `ARRAY_SIZE()` | Macro | `include/linux/kernel.h` | 编译期数组大小 |

## CONVENTIONS

### 代码风格（checkpatch.pl 强制）
- **缩进：tab，8 空格宽**（`$tabsize = 8`）
- **行宽：100 字符**（`$max_line_length = 100`，非 80）
- **函数大括号：下一行**（K&R 风格：`int foo()\n{`）
- **控制流/结构体大括号：同行**（`if (x) {`, `struct foo {`）
- **C99 `//` 注释：允许**（`$allow_c99_comments = 1`，区别于 Linux 内核）
- **无行尾空格**（ERROR: TRAILING_WHITESPACE）
- **`-Wall -Wextra -std=gnu11`** 全部编译生效
- 提交前跑 `perl scripts/checkpatch.pl --no-tree --strict <file.c>`

### 构建系统（Kbuild）
- 每目录声明 `obj-y := file1.o file2.o`
- 子目录递归：`obj-y += subdir/`（末尾 `/` 必须）
- 编译产物：`build-kbuild/agent`（单二进制）
- 详细模式：`make V=1` / `make V=2`（原因诊断）
- 架构选择：`ARCH=host`（默认）/ `ARCH=mips` / `ARCH=arm`

### 初始化（8 级 initcall）
pure(0) → core(1) → postcore(2) → arch(3) → subsys(4) → fs(5) → device(6) → late(7)
- `core_initcall`：message_bus, core_ipc, agent_hooks
- `postcore_initcall`：memory_store, session_store
- `subsys_initcall`：cron, heartbeat, http_proxy, skill_loader
- `device_initcall`：bus_init → buses + drivers + cores

### 驱动模型
- 3 条总线：`tool_bus`, `channel_bus`, `llm_bus`
- 匹配：默认 `strcmp(dev->name, drv->name)`
- probe 失败：设备留在总线上，等待依赖上线后 `bus_reprobe()`
- 技能模型：`skill_module` 三层（容器→设备→驱动）

### 内存管理
- 所有动态分配走 `kmalloc(size, GFP_KERNEL)` / `kfree()`（封装 malloc/free）
- cJSON：调用者用 `cJSON_Delete()` 释放解析结果，用 `free()` 释放打印结果
- `container_of()` 用于链表→结构体遍历

## ANTI-PATTERNS (THIS PROJECT)

- **PLANNER 永不可写代码**：`kernel/sched/class.c` 调度类约束
- **子 agent 不可递归委托**：`drivers/tool/tool_delegate.c` 禁止调用 `delegate_task`
- **Skill ≠ Driver**：Skill 是容器，不是驱动（见 `docs/BUS_MODEL.md`）
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
make mips|arm            # 交叉编译
make menuconfig          # 交互配置
make defconfig           # 默认配置
make test                # 45+ 单元测试
make clean|mrproper      # 清理
./run.sh                 # clean + build + run
./build-kbuild/agent     # 运行
./build-kbuild/agent --test  # 运行时自检
perl scripts/checkpatch.pl --no-tree --strict <file.c>
```

## NOTES

- **cJSON FIXME：** `cJSON_GetArraySize` 可能溢出（`int`←`size_t`），已知且不可修复
- **cJSON TODO：** `cJSON_Compare` O(n²) — 临时实现
- **`$(TOPDIR)/` 目录：** 构建副作用，应删除
- **CI 仅 ARM：** `.github/workflows/build-arm.yml` 用 cmake，与本地 Kbuild 路径不同
- **已提交 .o 文件：** `.gitignore` 已忽略但物理存在于源码树中 — `make clean` 清理
- **Ralph Loop：** 回合结束时有未完成 TODO 会强制追加警告（`extensions/module_ralph.c`）
- **中文注释：** 项目约定使用中文文档注释（如 `loop.h`："智能体主循环接口"）
