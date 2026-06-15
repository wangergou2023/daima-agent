#include "linux/sysctl.h"

#include "kernel/runtime.h"

err_t sysctl_init(void)
{
    return runtime_config_init();
}
