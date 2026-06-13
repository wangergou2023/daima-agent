#pragma once
#include "linux/init.h"

#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)

#define module_init(fn)  device_initcall(fn)
#define module_exit(fn)
