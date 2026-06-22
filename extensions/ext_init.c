/* 可选增强初始化入口。默认主链不依赖 extensions。 */

#include "ext_init.h"
#include "linux/printk.h"

/**
 * extensions_init — 默认不注册任何增强模块。
 *
 * @return 始终返回 0
 */
int extensions_init(void)
{
	pr_info("Extensions disabled in default mainline");
	return 0;
}
