# agent: 内核级 Agent 工程规划

## 目标

和 Linux 内核开发流程、代码风格、目录结构几乎一模一样。

## 完成度

```
目录            6.8内核                          agent        状态
────────────────────────────────────────────────────────────────────────
Documentation/  ✅ ReST文档 + kernel-doc        ❌                 待做
arch/           ✅ x86/arm/mips/riscv/...        ✅ host/mips/arm   完成
block/          ✅ 块层                          N/A              不需要
certs/          ✅ 证书系统                       N/A              不需要
crypto/         ✅ 加密API                        N/A              不需要
drivers/        ✅ 数百个驱动目录                  ✅ 10个驱动目录    基础完成
fs/             ✅ VFS + 文件系统                  ✅ fs/            基础完成
include/        ✅ linux/ asm/ generated/         ✅ linux/         基础完成
init/           ✅ main.c + Kconfig               ✅ main.c         完成
ipc/            ✅ 消息队列/信号量                 ✅ bus.c          完成
kernel/         ✅ sched/ time/ locking/ ...      ✅ sched/ time/   基础完成
lib/            ✅ 通用库                          ✅ lib/           完成
mm/             ✅ 内存管理                       N/A              不需要
net/            ✅ 网络栈                          ✅ net/           基础完成
scripts/        ✅ Kbuild/Kconfig/checkpatch      ✅ Kbuild/Kconfig 基础+照抄
security/       ✅ LSM                           ❌                 可选
sound/          ✅ ALSA                          → drivers/voice   完成
tools/          ✅ perf/selftests                 → test/           基础完成
usr/            ✅ initramfs                     N/A              不需要
virt/           ✅ KVM                           N/A              不需要

顶层文件:
COPYING         GPL-2.0                           MIT              待加
CREDITS         维护者列表                         ❌               待加
MAINTAINERS     子系统维护者                       ❌               待加
Kbuild          顶层构建                           → CMakeLists     需改
Kconfig         顶层配置                           ✅ Kconfig 完成
Makefile        顶层Makefile                      ✅               完成
README          项目说明                           ✅               完成
REPORTING-BUGS  bug报告指南                        ❌               待加
```

## Phase 1: 顶层文件补齐

```
COPYING         →  MIT License (替换 LICENSE.md)
CREDITS         →  贡献者列表
MAINTAINERS     →  子系统维护者 (格式照抄内核)
REPORTING-BUGS  →  bug report 指南
```

## Phase 2: scripts/ 补齐

照抄内核 (能直接用就用):
```
scripts/checkpatch.pl  ← 内核原样复制
scripts/kernel-doc     ← 内核原样复制
scripts/get_maintainer.pl  ← 内核原样复制 (可选)
```

## Phase 3: Kbuild 递归构建

替换 cmake 为真正的 Kbuild 递归:
```
顶层 Makefile
  → init/Makefile       (obj-y += main.o bootstrap.o)
  → kernel/Makefile     (obj-y += loop.o hooks.o sched/)
  → kernel/sched/Makefile (obj-y += core.o class.o agent.o)
  → ipc/Makefile        (obj-y += bus.o)
  → lib/Makefile        (obj-y += text.o base64.o log.o)
  → net/Makefile        (obj-y += http.o tls.o proxy.o)
  → fs/Makefile         (obj-y += paths.o fs.o)
  → drivers/llm/Makefile (obj-y += llm_proxy.o payload.o)
  → arch/host/Makefile  (obj-y += ...)
```

每个目录都有 `obj-y` / `obj-m`。

## Phase 4: kernel/ 子系统完善

```
kernel/sched/       ✅ 完成
kernel/time/        ✅ 完成
kernel/locking/     待实现 (mutex.c, spinlock.c)
kernel/printk/      待实现 (printk.c → lib/log.c 移过来)
kernel/irq/         待实现 (中断框架 → agent 信号处理)
kernel/power/       待实现 (suspend/resume → agent 暂停/恢复)
kernel/fork.c       待实现 (agent 创建 → copy_process 风格)
kernel/exit.c       待实现 (agent 销毁 → do_exit 风格)
kernel/sysctl.c     待实现 (运行时参数 → /proc/sys 风格)
kernel/workqueue.c  ✅ 完成
```

## Phase 5: include/ 头文件规范

```
include/linux/autoconf.h    ✅
include/linux/compiler.h    ✅
include/linux/init.h        ✅
include/linux/module.h      ✅
include/linux/mutex.h       ✅
include/linux/sched.h       ✅
include/linux/types.h       ✅
include/linux/workqueue.h   ✅
include/linux/err.h         待加 (err.h → linux/err.h)
include/linux/printk.h      待加 (log.h → linux/printk.h)
include/linux/kernel.h      待加 (工具宏如 ARRAY_SIZE, container_of)
include/linux/slab.h        待加 (kmalloc/kfree 封装)
include/linux/list.h        待加 (内核链表)
include/generated/          待加 (构建时生成的头文件)
include/asm/host/           待加 (平台相关)
include/asm/mips/
include/asm/arm/
```

## Phase 6: tools/ 测试

```
tools/testing/selftests/sched/   调度器测试
tools/testing/selftests/llm/     LLM 驱动测试
tools/testing/selftests/ipc/     消息总线测试
tools/perf/                      性能测试 (可选)
```

## Phase 7: 细节对齐

```
- 内核代码风格: checkpatch.pl 检查
- 注释风格: /** kernel-doc */ 格式
- 命名: struct_ops, xxx_init/xxx_exit
- printk 日志级别: KERN_ERR/KERN_WARNING/KERN_INFO
- 错误码: -EINVAL, -ENOMEM (映射 agent_err_t)
```

## 执行顺序

| 优先级 | Phase | 工作量 | 影响 |
|--------|-------|--------|------|
| 🔴 P0 | P1 顶层文件 | 30min | 外观像内核 |
| 🔴 P0 | P2 scripts/ | 30min | 开发工具像内核 |
| 🟡 P1 | P4 kernel/子系统 | 2h | 内核化核心逻辑 |
| 🟡 P1 | P5 include/ | 1h | 头文件规范 |
| 🟢 P2 | P3 Kbuild | 3h | 大改动, 风险高 |
| 🟢 P2 | P6 tools/ | 1h | 测试规范 |
| ⚪ P3 | P7 细节 | 持续 | 渐进优化 |
