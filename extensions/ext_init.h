/* 扩展模块初始化调度器：集中调用所有 8 个模块的 init 函数。 */

#ifndef _EXT_INIT_H
#define _EXT_INIT_H

#include "linux/init.h"

int __init intent_module_init(void);
int __init interview_module_init(void);
int __init plan_module_init(void);
int __init ralph_module_init(void);
int __init roles_module_init(void);
int __init router_module_init(void);
int __init sched_module_init(void);
int __init team_module_init(void);

int extensions_init(void);

#endif /* _EXT_INIT_H */
