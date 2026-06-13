#pragma once
#include "linux/init.h"

struct daima_driver {
    const char *name;
    int (*probe)(void);
    void (*remove)(void);
};

#define daima_driver_register(drv) \
    static int __init _driver_probe_##drv(void) { return daima_driver_probe(&drv); } \
    device_initcall(_driver_probe_##drv)
