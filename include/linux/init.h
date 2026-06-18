/* 初始化调用链接口：8 级 initcall 宏，按优先级顺序执行初始化函数。 */

#pragma once

/* initcall 函数指针类型 */
typedef int (*initcall_t)(void);

/* __init/__exit 占位符（当前不做段属性处理） */
#define __init
#define __exit

/* 8 级初始化调用：数字越小优先级越高 */
/* 0：最早期初始化（在 core_initcall 之前） */
#define pure_initcall(fn)     __initcall(fn, 0)
/* 1：核心初始化（IPC、钩子、消息总线） */
#define core_initcall(fn)     __initcall(fn, 1)
/* 2：核心后初始化（内存存储、会话存储） */
#define postcore_initcall(fn) __initcall(fn, 2)
/* 3：平台架构初始化 */
#define arch_initcall(fn)     __initcall(fn, 3)
/* 4：子系统初始化（cron、心跳、HTTP 代理、技能加载器） */
#define subsys_initcall(fn)   __initcall(fn, 4)
/* 5：文件系统初始化 */
#define fs_initcall(fn)       __initcall(fn, 5)
/* 6：设备初始化（总线初始化 → 驱动注册 → 设备探测） */
#define device_initcall(fn)   __initcall(fn, 6)
/* 7：延迟初始化 */
#define late_initcall(fn)     __initcall(fn, 7)

#ifdef __APPLE__
/* macOS：放入 __DATA 段的指定 section */
#define __initcall(fn, level) \
	static initcall_t __initcall_##fn __attribute__((used)) \
		__attribute__((section("__DATA,.__initcall" #level))) = fn
#else
/* Linux/ELF：放入 .initcall 段对应级别 */
#define __initcall(fn, level) \
	static initcall_t __initcall_##fn __attribute__((used)) \
		__attribute__((section(".initcall" #level ".init"))) = fn
#endif

struct driver;
/**
 * 对驱动执行 probe（设备已绑定时调用）。
 * @param drv  驱动指针
 * @return 0 成功，负数错误码
 */
int driver_probe(struct driver *drv);

/* 总线子系统初始化 */
int bus_init(void);

/**
 * 执行完整的 8 级初始化链：从 pure(0) 到 late(7) 依次调用。
 * @return 0 成功，负数错误码
 */
int do_basic_setup(void);
