/* 驱动核心：driver_probe() 封装 driver->probe() 调用。 */

#include "linux/driver.h"
#include "linux/printk.h"

/** 调用驱动的 probe 函数。@param drv 驱动指针 @return probe() 返回值或 -1 */
int driver_probe(struct driver *drv)
{
    if (!drv || !drv->probe) return -1;
    pr_info("probe: %s", drv->name);
    return drv->probe(NULL);
}
