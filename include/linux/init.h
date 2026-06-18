/* 手动 4 级初始化链（core/postcore/subsys/device），不再使用 section 放置机制。
 * do_basic_setup() 按序手动调用各阶段注册函数。
 */

#pragma once

/* initcall 函数指针类型 */
typedef int (*initcall_t)(void);

/* __init/__exit 占位符（当前不做段属性处理） */
#define __init
#define __exit

/* 4 级手动初始化链 — 保留宏仅用于文档标注 */
#define core_initcall(fn)		/* 第 1 级：IPC、钩子、消息总线 */
#define postcore_initcall(fn)		/* 第 2 级：内存存储、会话存储 */
#define subsys_initcall(fn)		/* 第 4 级：cron、心跳、代理、技能 */
#define device_initcall(fn)		/* 第 6 级：总线 → 驱动 → 核间启动 */

/* 总线子系统初始化 */
int bus_init(void);

/* 执行完整 4 级手动初始化链 */
int do_basic_setup(void);
