# include/linux/ — 内核风格公共 API

**17 头文件，内核命名空间。总线模型、调度、锁、IPC、内存管理的公共 API。**

## STRUCTURE

```
include/linux/
├── kernel.h compiler.h types.h list.h     # 核心：container_of、ARRAY_SIZE、__uN 类型、双向循环链表
├── bus.h driver.h                          # 总线/驱动模型（3 总线：tool/channel/llm）
├── slab.h                                  # 内存分配（kmalloc/kzalloc/kfree，GFP_KERNEL）
├── mutex.h spinlock.h                      # 锁（pthread mutex + pthread spinlock 内联宏封装）
├── core_task.h                             # 3 核 IPC（SCHEDULER=0 / MEMORY=1 / EXECUTOR=2）
├── sched.h sched_service.h                 # 调度器（Agent 运行队列、PLANNER/EXECUTOR/REVIEWER 三类）
├── init.h                                  # 4 级手动初始化链
├── printk.h workqueue.h                   # 日志桥接 / 心跳服务
```

## WHERE TO LOOK

| Task | Header | Key API |
|------|--------|---------|
| 驱动注册与探测 | bus.h, driver.h | `bus_create`, `device_register`, `driver_register`, `bus_probe` |
| 父结构体内省 | kernel.h | `container_of(ptr, type, member)` |
| 链表遍历 | list.h | `list_add`, `list_for_each_entry`, `list_for_each_entry_safe` |
| 内存分配 | slab.h | `kmalloc(size, GFP_KERNEL)`, `kzalloc`, `kfree` |
| 临界区保护 | mutex.h, spinlock.h | `mutex_lock/unlock`, `spin_lock/unlock` |
| 核间通信 | core_task.h | `core_send/recv/reply`, `core_ipc_init` |
| Agent 调度 | sched_service.h | `sched_dispatch`, `sched_start`, `sched_wait`, `sched_merge` |
| 初始化链注册 | init.h | `device_initcall(fn)`（空宏） |
| 日志输出 | printk.h | `pr_info/pr_err/pr_warn/pr_debug` |

## CONVENTIONS

- `container_of()` 是所有 `list_head` 遍历的基元：通过成员指针反推父结构体
- `struct driver` **必须是嵌入结构体的第一个字段**（`container_of` 偏移 = 0 的要求）
- 内存分配一律走 `kmalloc/kfree`，不直接用 `malloc/free`
- 日志一律走 `pr_info/pr_err`，不用 `printf`
- 缩进 tab 8 字符，行宽 ≤100，`/* */` 注释

## ANTI-PATTERNS

- 不可直写 `cJSON.valueint` — 用 `cJSON_SetNumberValue()`（`include/cjson.h:116` DEPRECATED）
- 子 agent 不可递归委托（`delegate_task` 禁止调用自身）
- PLANNER 永不可写代码（`kernel/sched/class.c` 调度类约束）
