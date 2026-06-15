#include "linux/driver.h"
#include "linux/printk.h"
int driver_probe(struct driver *drv)
{
    if (!drv || !drv->probe) return -1;
    pr_info("probe: %s", drv->name);
    return drv->probe(NULL);
}
