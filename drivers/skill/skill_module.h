/* skill_module: 容器层，管理一组 device 的 load/unload 生命周期。
 * 对应内核的 struct module / insmod / rmmod。
 */
#pragma once

#include "err.h"

struct skill_module {
    const char *name;                       /* "pptx" */
    const char *description;                /* 来自 SKILL.md front matter */

    int (*probe)(struct skill_module *sm);  /* 检查依赖是否满足 */
    int (*load)(struct skill_module *sm);   /* 注册所有 device */
    void (*unload)(struct skill_module *sm);/* 卸载所有 device */

    int loaded;                             /* 是否已加载 */
    void *priv;                             /* 模块私有数据 */
};

int skill_module_probe(struct skill_module *sm);
int skill_module_load(struct skill_module *sm);
void skill_module_unload(struct skill_module *sm);