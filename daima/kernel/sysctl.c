#include "linux/sysctl.h"

#include "kernel/runtime.h"

daima_err_t sysctl_init(void)
{
    return runtime_config_init();
}
