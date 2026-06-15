/* skill_module 生命周期管理 */
#include "drivers/skill/skill_module.h"
#include "linux/printk.h"

int skill_module_probe(struct skill_module *sm)
{
    if (!sm) return -1;
    if (sm->probe) return sm->probe(sm);
    return 0;
}

int skill_module_load(struct skill_module *sm)
{
    if (!sm) return -1;
    if (sm->loaded) return 0;
    if (sm->load) {
        int ret = sm->load(sm);
        if (ret != 0) return ret;
    }
    sm->loaded = 1;
    pr_info("skill_module: %s loaded", sm->name);
    return 0;
}

void skill_module_unload(struct skill_module *sm)
{
    if (!sm || !sm->loaded) return;
    if (sm->unload) sm->unload(sm);
    sm->loaded = 0;
    pr_info("skill_module: %s unloaded", sm->name);
}