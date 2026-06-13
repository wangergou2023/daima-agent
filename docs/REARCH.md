# daima-agent 内核框架重构规划

## 目标

按 Linux 内核子系统 1:1 映射，越像越好。每个 Phase 独立可交付。

---

## Phase 1: 驱动模型 (drivers/)

**目标**: 每个驱动 register/probe/remove，类似 `struct platform_driver`

```
drivers/llm/
├── llm_driver.c      ← module_platform_driver(llm_driver)
├── llm_ops.c         ← 操作集 (open/chat/close)
├── payload.c         ← OpenAI/Anthropic payload
└── proto.h           ← 内部协议头

drivers/channel/
├── channel_core.c    ← channel_register/channel_unregister
├── feishu/
│   └── feishu_driver.c ← 实现 channel_ops
├── vector/
│   └── vector_driver.c
└── gateway/
    └── ws_driver.c

drivers/tool/
├── tool_bus.c        ← tool_register/tool_unregister (≈ driver model)
├── file_tool.c
├── terminal_tool.c
└── ...

每个驱动: 
  module_platform_driver(xxx) → 自动注册
  xxx_probe()              → 初始化
  xxx_remove()             → 清理
```

**新增文件**: 每个驱动目录加 `*_driver.c`，`register`/`unregister` 统一接口

---

## Phase 2: 初始化链 (init/)

**目标**: `init/main.c` 用 `do_initcalls()` 风格启动

```
init/main.c:
  asmlinkage void __init start_kernel(void) {
      setup_arch();
      mm_init();
      sched_init();
      init_IRQ();        → ipc/bus_init()
      init_timers();     → kernel/time/hrtimer_init()
      init_workqueues(); → kernel/workqueue_init()
      driver_init();
      do_initcalls();
      ...
  }

等级:
  pure_initcall(level0)   ← arch/ (最优先)
  core_initcall(level1)   ← kernel/ ipc/ lib/
  postcore_initcall(level2)
  arch_initcall(level3)   ← drivers/ platform
  subsys_initcall(level4) ← drivers/ channel/tool
  fs_initcall(level5)     ← fs/
  device_initcall(level6) ← drivers/
  late_initcall(level7)   ← extensions/
```

**实现**: `init/main.c` 用宏和函数指针表实现分级初始化

---

## Phase 3: 子系统完善 (kernel/)

```
kernel/
├── sched/           ✅ 已完成 (core/class/agent)
├── time/            ← cron.c → hrtimer.c (高精度定时器)
│   └── timer.c
├── workqueue.c      ← heartbeat → workqueue (延迟工作)
├── power/           ← 语音唤醒 → suspend/resume
│   └── wakeup.c
├── locking/         ← FreeRTOS mutex → mutex.h/spinlock.h
│   ├── mutex.c
│   └── spinlock.c
├── sysctl.c         ← runtime_config → /proc/sys 风格
├── capability.c     ← tool 权限管理
└── fork.c           ← 多agent 创建/回收
```

---

## Phase 4: 头文件规范 (include/)

```
include/
├── linux/           ← kernel style prefix
│   ├── sched.h
│   ├── init.h       ← initcall 宏
│   ├── module.h     ← MODULE_LICENSE, module_init
│   ├── driver.h     ← struct platform_driver
│   ├── mutex.h
│   ├── workqueue.h
│   ├── wait.h       ← wait_event/wake_up
│   ├── err.h
│   ├── types.h
│   └── compiler.h   ← likely/unlikely, __init
├── asm/             ← 平台相关
│   ├── host/atomic.h
│   ├── mips/atomic.h
│   └── arm/atomic.h
├── generated/       ← 构建生成
│   └── autoconf.h
└── cJSON.h          ← 第三方
```

**命名规范**:
- 用户空间 API: 无前缀，如 `agent_send_message()`
- 内核内部: `__` 前缀，如 `__agent_loop()`
- 驱动接口: `drm_`/`net_`/`tty_` 等子系统前缀

---

## Phase 5: 模块系统 (extensions/)

```
extensions/
├── Makefile          ← obj-m += module_intent.o
├── module_intent.c   ← module_init(intent_init) / module_exit(intent_exit)
├── module_sched.c
├── module_plan.c
└── ...

每个模块:
  MODULE_LICENSE("GPL");
  MODULE_AUTHOR("daima");
  MODULE_DESCRIPTION("Intent Gate Agent Extension");
  
  static int __init intent_init(void) { register_hook(); return 0; }
  static void __exit intent_exit(void) { unregister_hook(); }
  module_init(intent_init);
  module_exit(intent_exit);
```

---

## Phase 6: 基础设施 (scripts/ tools/ Documentation/)

```
scripts/
├── kconfig.py        ✅ 已完成
├── Makefile.build    ← 编译规则
├── checkpatch.pl     ← 代码风格检查
└── kernel-doc        ← 文档生成

tools/testing/selftests/
├── sched/            ← 调度器测试
├── llm/              ← LLM 驱动测试
└── run_tests.sh

Documentation/
├── admin-guide/      ← 用户手册
├── driver-api/       ← 驱动文档
└── core-api/         ← 内核 API 文档
```

---

## Phase 7: SMP 调度强化

**目标**: 真正的多核调度

```
kernel/sched/
├── core.c         ← schedule() 主循环
├── fair.c         ← CFS 公平调度
│   vruntime → 各agent的平均响应时间
│   min_vruntime → 选等待最久的agent
├── rt.c           ← 实时调度
│   PLANNER 总是最先跑
│   REVIEWER 可被 EXECUTOR 抢占
├── deadline.c     ← 最后期限调度
│   每个agent有 deadline
│   超时自动降级/终止
├── idle.c         ← idle task
│   无agent时 wait_for_event()
├── topology.c     ← "CPU" 拓扑
│   3个HTTP连接 ≈ 3个物理核
│   arch/host vs arch/mips 不同拓扑
├── loadavg.c      ← 负载统计
└── stats.c        ← /proc/sched 统计
```

---

## 优先级

| Phase | 价值 | 复杂度 | 建议 |
|-------|------|--------|------|
| P1 驱动模型 | ⭐⭐⭐ | 中 | 接口统一，值得做 |
| P2 初始化链 | ⭐⭐⭐ | 低 | 改动小，收益高 |
| P3 子系统完善 | ⭐⭐ | 中 | 按需做 |
| P4 include/ | ⭐⭐⭐ | 中 | 命名规范，值得做 |
| P5 模块系统 | ⭐⭐ | 低 | 已有 module_*，改名即可 |
| P6 基础设施 | ⭐ | 低 | 锦上添花 |
| P7 SMP强化 | ⭐⭐ | 高 | 等真正需要时做 |
