#include "linux/driver.h"
#include "linux/printk.h"
int daima_driver_probe(struct daima_driver *drv)
{
    if (!drv || !drv->probe) return -1;
    pr_info("probe: %s", drv->name);
    return drv->probe();
}
