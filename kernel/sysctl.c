/* 系统控制接口：sysctl_init() 代理到 runtime_config_init()。 */

#include "linux/sysctl.h"

#include "kernel/runtime.h"

err_t sysctl_init(void)
{
    return runtime_config_init();
}
