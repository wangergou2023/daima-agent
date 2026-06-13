#include "linux/driver.h"
#include "lib/log.h"

static const char *TAG = "driver";

int daima_driver_probe(struct daima_driver *drv)
{
    if (!drv || !drv->probe) return -1;
    DAIMA_LOGI(TAG, "probe: %s", drv->name);
    return drv->probe();
}
