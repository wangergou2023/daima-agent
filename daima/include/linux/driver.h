#pragma once
#include "linux/init.h"

struct driver {
    const char *name;
    int (*probe)(void);
    void (*remove)(void);
};

#define driver_register(drv) \
    static int __init _driver_probe_##drv(void) { return driver_probe(&drv); } \
    device_initcall(_driver_probe_##drv)
