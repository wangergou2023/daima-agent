/* 扩展模块初始化调度器：仅注册非核心增强模块。 */

#include "ext_init.h"
#include "linux/printk.h"

/**
 * extensions_init — 仅注册可选增强模块。
 *
 * @return 始终返回 0
 */
int extensions_init(void)
{
	int err;

	pr_info("Registering optional extension hooks...");

	err = sched_module_init();
	if (err) pr_warn("sched_module_init failed: %d", err);

	err = team_module_init();
	if (err) pr_warn("team_module_init failed: %d", err);

	pr_info("Optional extension hooks registered");
	return 0;
}
