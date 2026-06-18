/* 系统控制接口：运行时配置读取与管理。 */

#pragma once

#include "err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 sysctl 运行时配置系统。
 * @return 0 成功，负数错误码
 */
err_t sysctl_init(void);

#ifdef __cplusplus
}
#endif
