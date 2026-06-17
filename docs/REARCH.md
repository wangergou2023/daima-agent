# Daima Agent: 内核级架构工程规划

## 目标

以 Linux 内核的开发流程、代码风格、目录结构为蓝本构建 AI Agent。

## 完成度总览

```
目录            6.8 内核                        Agent                状态
──────────────────────────────────────────────────────────────────────
arch/           ✅ x86/arm/mips/riscv/...       ✅ host/mips/arm      完成
block/          ✅ 块层                          N/A                  不需要
certs/          ✅ 证书系统                      N/A                  不需要
crypto/         ✅ 加密 API                      N/A                  不需要
drivers/        ✅ 数百个驱动目录                ✅ 10 个驱动目录      完成
fs/             ✅ VFS + 文件系统                ✅ fs/                完成
include/        ✅ linux/ asm/ generated/        ✅ linux/ + generated 完成
init/           ✅ main.c + Kconfig              ✅ main.c             完成
ipc/            ✅ 消息队列/信号量               ✅ bus.c (消息总线)     完成
                ✅ bus_device / bus_init           ✅ bus/driver/device 核心
                ✅ bus_channel / bus_llm           ✅ 通道/LLM 总线驱动
kernel/         ✅ sched/ time/ locking/ ...     ✅ sched/ time/       基础完成
                                                ✅ printk/ irq/ workqueue
                                                ❌ locking/ power/ fork/exit
lib/            ✅ 通用库                        ✅ lib/               完成
mm/             ✅ 内存管理                      N/A                  不需要
net/            ✅ 网络栈                        ✅ net/               完成
scripts/        ✅ Kbuild/Kconfig/checkpatch     ✅ Kbuild/Kconfig     完成
                                                ✅ checkpatch.pl
                                                ✅ get_maintainer.pl
                                                ❌ kernel-doc
security/       ✅ LSM                           ❌                    可选
sound/          ✅ ALSA                          → drivers/voice       完成
tools/          ✅ perf/selftests                → test/               基础完成
usr/            ✅ initramfs                     N/A                  不需要
virt/           ✅ KVM                           N/A                  不需要

顶层文件:
COPYING         ✅ GPL-2.0                       ✅ MIT                完成
CREDITS         ✅ 维护者列表                    ✅                    完成
MAINTAINERS     ✅ 子系统维护者                  ✅                    完成
Makefile        ✅ Kbuild                        ✅ Kbuild             完成
Kconfig         ✅ 顶层配置                      ✅ Kconfig            完成
README          ✅ 项目说明                      ✅                    完成
REPORTING-BUGS  ✅ Bug 报告指南                  ✅                    完成
```

## 待完成项

### 🔴 高优先级

| 任务 | 说明 | 预估 |
|------|------|------|
| kernel/locking/ | mutex.c, spinlock.c | 1h |
| kernel/fork.c | Agent 创建 (copy_process 风格) | 1h |
| kernel/exit.c | Agent 销毁 (do_exit 风格) | 0.5h |
| kernel/sysctl.c | 运行时参数 (/proc/sys 风格) | 1h |
| scripts/kernel-doc | 内核文档生成工具 | 0.5h |
| 热插拔 reprobe 链 | tool/skill 依赖上线 → 自动可用 | 1h |
| 记忆核真正异步压缩 | 把 context_compressor 改成消息可传递的异步版本 | 2h |

### 🟡 中优先级

| 任务 | 说明 | 预估 |
|------|------|------|
| kernel/power/ | suspend/resume → Agent 暂停/恢复 | 1h |
| include/linux/err.h | 错误码头文件 | 0.5h |
| include/linux/autoconf.h | 构建生成头文件 | 0.5h |
| security/ | LSM 风格安全模块 (可选) | 2h |

### 🟢 低优先级

| 任务 | 说明 | 预估 |
|------|------|------|
| tools/testing/selftests/ | sched/ llm/ ipc/ 专项测试 | 1h |
| tools/perf/ | 性能测试 | 1h |
| include/asm/{host,mips,arm}/ | 平台相关头文件 | 0.5h |

## 完成项对照

### 顶层文件 ✅

```
COPYING         → MIT License
CREDITS         → 贡献者列表
MAINTAINERS     → 子系统维护者 (格式照抄内核)
REPORTING-BUGS  → Bug 报告指南
```

### scripts/ ✅

```
scripts/checkpatch.pl       → 内核原样复制
scripts/get_maintainer.pl   → 维护者查询
scripts/Makefile.build      → obj-y 递归编译引擎
scripts/Kbuild.include      → Kbuild 编译规则
scripts/Kconfig.include     → Kconfig 语法支持
scripts/menuconfig.py       → ncurses TUI 配置
scripts/kconfig.py          → Kconfig 解析器
```

### Kbuild 递归构建 ✅

```
顶层 Makefile
  → init/Makefile         (obj-y += main.o bootstrap.o)
  → kernel/Makefile       (obj-y += loop.o hooks.o sched/)
  → kernel/sched/Makefile (obj-y += core.o class.o agent.o)
  → ipc/Makefile          (obj-y += bus.o bus_device.o bus_init.o bus_channel.o bus_llm.o)
  → lib/Makefile          (obj-y += text.o base64.o log.o)
  → net/Makefile          (obj-y += http.o tls.o proxy.o)
  → fs/Makefile           (obj-y += paths.o fs.o)
  → drivers/llm/Makefile  (obj-y += llm_proxy.o payload.o)
  → arch/host/Makefile    (obj-y += ...)
```

### include/ 头文件 ✅

```
include/linux/compiler.h    ✅
include/linux/init.h        ✅
include/linux/module.h      ✅
include/linux/mutex.h       ✅
include/linux/sched.h       ✅
include/linux/types.h       ✅
include/linux/workqueue.h   ✅
include/linux/printk.h      ✅
include/linux/kernel.h      ✅ (ARRAY_SIZE, container_of)
include/linux/slab.h        ✅ (kmalloc/kfree)
include/linux/bus.h         ✅ (bus_type / device / dependency / API)
include/linux/driver.h       ✅ (增强 struct driver)
include/linux/list.h         ✅ (内核链表)
include/generated/          ✅ (构建生成)
```

### Bus/Driver/Device 模型 ✅

```
ipc/bus_device.c       ✅ bus_create/destroy, device/driver_register
                       ✅ bus_probe/probe_all/reprobe
                       ✅ bus_find_device/bus_device_exists
ipc/bus_init.c         ✅ 3 条总线实例 (tool/channel/llm)
ipc/bus_channel.c      ✅ feishu/vector/voice/gateway 通道驱动
ipc/bus_llm.c          ✅ openai_compatible + anthropic_compatible

tool_bus:  25 tool_driver + 25 tool_device → name match 独立绑定
channel_bus: feishu/vector/voice/gateway → name match
llm_bus:   openai_compatible + anthropic_compatible
skill_module: ✅ probe/load/unload, bus_device_exists 依赖检查
tool_device/tool_driver: ✅ 声明层/执行层拆分, container_of 执行
custom_tools.json: ✅ Level 2 零编译扩展 tool
of_populate(): ✅ Level 3 JSON → bus/device 统一解析
3-core IPC: ✅ scheduler / executor / memory 三核分工
self-test: ✅ 7/7 通过（含 LLM 端到端）
```


### 多核异步调度 ✅

```
include/linux/core_task.h  ✅ core_task 协议（type/status/payload/result）
ipc/core_ipc.c             ✅ 3 个独立队列（scheduler/executor/memory）
kernel/executor_core.c     ✅ 无状态工具执行 worker
kernel/memory_core.c       ✅ 无状态会话/上下文 worker
kernel/turn_dispatch.c     ✅ fire-and-forget 调度层
kernel/turn_context.c      ✅ turn 快照存储
kernel/loop.c              ✅ async multiplex：新消息 + 核间回复
```

### 细节对齐

```
- 代码风格: checkpatch.pl 检查
- 注释风格: /** kernel-doc */ 格式
- 命名规范: struct_ops, xxx_init/xxx_exit
- 日志级别: pr_err/pr_warn/pr_info (映射 printk)
- 错误码: -EINVAL, -ENOMEM 等
```
