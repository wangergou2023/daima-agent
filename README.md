# Daima Agent

嵌入式 AI Agent，基于 Linux 内核风格架构。**C11 + Kbuild，单二进制**。

> 设计文档：[总线模型](docs/BUS_MODEL.md) | [架构规划](docs/REARCH.md)

## 目录

- [快速开始](#快速开始)
- [架构概览](#架构概览)
- [构建命令](#构建命令)
- [内核风格特性](#内核风格特性)
- [Agent 功能](#agent-功能)

## 快速开始

```bash
make menuconfig    # 配置功能
make               # 编译
make test          # 测试
./build-kbuild/agent  # 运行
```

## 架构概览

目录结构与 Linux 内核 1:1 映射：

### 核心子系统

```
agent/
├── init/main.c                         ← kernel/init/main.c
├── kernel/
│   ├── sched/{core,class,agent}.c      ← kernel/sched/ (多核调度)
│   ├── time/timer.c                    ← kernel/time/ (hrtimer)
│   ├── printk/printk.c                 ← kernel/printk/
│   ├── irq/irq.c                       ← kernel/irq/
│   ├── workqueue.c                     ← kernel/workqueue.c
│   ├── sysctl.c                        ← kernel/sysctl.c
│   ├── loop / hooks / intent / plan / roles / router
│   └── turn_{common,prepare,run,exec,finish,persist}
├── ipc/bus.c                           ← kernel/ipc/
├── lib/                                ← kernel/lib/
├── net/                                ← kernel/net/
├── fs/                                 ← kernel/fs/
```

### 驱动层

```
├── drivers/
│   ├── llm/        LLM 驱动
│   ├── channel/    通道驱动 (飞书 / Vector / WebSocket)
│   ├── tool/       工具驱动
│   ├── memory/     存储驱动
│   ├── skill/      技能驱动
│   └── voice, vision, platform, pet, audio
```

### 平台 & 模块 & 头文件

```
├── arch/{host,mips,arm}/               ← kernel/arch/
├── extensions/module_*.c               ← LKM (可加载模块)
├── include/linux/                      ← include/linux/
│   ├── kernel.h     ARRAY_SIZE / container_of
│   ├── slab.h       kmalloc / kfree
│   ├── list.h       内核链表
│   ├── printk.h     pr_err / pr_info
│   ├── mutex / compiler / types / sched
│   └── init / module / workqueue
├── include/generated/autoconf.h        ← 构建生成
└── Kconfig                             ← 功能开关
```

### 构建脚本 & 顶层文件

```
scripts/
├── Kbuild.include    Kbuild 编译引擎
├── Makefile.build    obj-y 递归编译
├── checkpatch.pl     代码风格检查 (照抄内核)
├── menuconfig.py     ncurses 交互配置
└── kconfig.py        Kconfig 解析器

顶层文件:
├── COPYING           MIT License
├── CREDITS           贡献者
├── MAINTAINERS       子系统维护者
├── REPORTING-BUGS    Bug 报告指南
├── Makefile          Kbuild 顶层
└── .config           运行时配置
```

## 构建命令

| 命令 | 说明 |
|------|------|
| `make` | Kbuild 编译 → `build-kbuild/agent` |
| `make clean` | 清理编译产物 |
| `make mrproper` | 清理 + 删除 `.config` |
| `make menuconfig` | ncurses 交互配置 |
| `make defconfig` | 默认配置 |
| `make test` | 运行测试 |
| `make mips` | MIPS 交叉编译 |
| `make arm` | ARM 交叉编译 |

## 内核风格特性

| 特性 | 实现 |
|------|------|
| 构建系统 | Kbuild 递归 + `obj-y` (零 cmake) |
| 配置系统 | Kconfig + `make menuconfig` (ncurses) |
| 驱动模型 | `struct agent_driver` + `probe()/remove()` |
| 模块系统 | `extensions/module_*` + `MODULE_LICENSE` |
| 初始化链 | `core_initcall` → `device_initcall` 8 级 |
| 平台抽象 | `arch/{host,mips,arm}/` + per-arch Makefile |
| 代码风格 | `checkpatch.pl` + kernel-doc |
| 多核调度 | `kernel/sched/{core,class,agent}.c` |
| [总线模型](docs/BUS_MODEL.md) | `bus_type` + `device` + `driver` + `probe()` |

## Agent 功能

- LLM 调用 (OpenAI / Anthropic 协议)
- 多 Agent 并行调度 (PLANNER + EXECUTOR + REVIEWER)
- 意图分类 + 角色路由 + 计划评审
- 飞书 / Vector / WebSocket 多通道接入
- 工具系统 (文件 / 终端 / Web 抓取 / cron / skill)
- 会话存储 / 压缩 / 恢复
- Hashline 安全编辑
- Prometheus 访谈模式
- Todo 强制执行 + Ralph Loop
- 模型回退 + 分类路由
