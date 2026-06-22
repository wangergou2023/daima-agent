/* 技能加载门面。 */

#include "drivers/skill/skill_loader.h"
#include "drivers/skill/skill_builtin.h"
#include "drivers/skill/skill_summary.h"

#include "linux/printk.h"

err_t skill_loader_init(void)
{
    pr_info("Initializing skills system");
    skill_summary_init();
    skill_builtin_install_all();
    pr_info("Skills system ready (%d built-in)", (int)skill_builtin_count());
    return 0;
}

size_t skill_loader_build_summary_for_channel(const char *channel, char *buf, size_t size)
{
    return skill_summary_build_for_channel(channel, buf, size);
}

size_t skill_loader_build_summary(char *buf, size_t size)
{
    return skill_loader_build_summary_for_channel(NULL, buf, size);
}
