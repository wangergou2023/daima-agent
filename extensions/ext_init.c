/* 扩展模块初始化调度器：按依赖顺序调用所有 LKM 风格模块的 init 函数。 */

#include "ext_init.h"
#include "linux/printk.h"

/**
 * extensions_init — 集中注册所有扩展模块钩子。
 *
 * 调用顺序反映 Turn 流水线的介入顺序：
 *   intent → roles → plan → router → interview → sched → team → ralph
 *
 * 注册失败时输出警告但不终止启动。
 *
 * @return 始终返回 0
 */
int extensions_init(void)
{
	int err;

	pr_info("Registering extension hooks...");

	err = intent_module_init();
	if (err) pr_warn("intent_module_init failed: %d", err);

	err = roles_module_init();
	if (err) pr_warn("roles_module_init failed: %d", err);

	err = plan_module_init();
	if (err) pr_warn("plan_module_init failed: %d", err);

	err = router_module_init();
	if (err) pr_warn("router_module_init failed: %d", err);

	err = interview_module_init();
	if (err) pr_warn("interview_module_init failed: %d", err);

	err = sched_module_init();
	if (err) pr_warn("sched_module_init failed: %d", err);

	err = team_module_init();
	if (err) pr_warn("team_module_init failed: %d", err);

	err = ralph_module_init();
	if (err) pr_warn("ralph_module_init failed: %d", err);

	pr_info("Extension hooks registered");
	return 0;
}
