/* Prometheus 访谈模块：当单角色 IMPLEMENT 意图信息不足时，以提问题代替执行（replace_run 钩子）。 */

#include "hooks.h"
#include "turn_finish.h"
#include "state.h"
#include "interview.h"
#include "autoconf.h"
#include "linux/module.h"
#include "linux/printk.h"

#include <stdlib.h>
#include <string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("agent");
MODULE_DESCRIPTION("Agent Extension: prometheus_interview");

/**
 * replace_run 钩子：检查 IMPLEMENT 意图是否需要访谈澄清。
 * 若用户需求不明确（单角色、无计划），以问题列表代替执行。
 * @param msg             入站消息
 * @param system_prompt   system prompt（未使用）
 * @param messages        JSON 消息数组（未使用）
 * @param tools_json      工具 JSON（未使用）
 * @param out_final_text  输出：访谈问题文本（需调用方释放）
 * @return 进入访谈返回 0，否则返回 ERR_FAIL
 */
static err_t replace_run(struct message *msg, char *system_prompt,
                               cJSON *messages, const char *tools_json,
                               char **out_final_text)
{
    (void)system_prompt;
    (void)messages;
    (void)tools_json;
#if AGENT_EXTENSIONS_ENABLED
    if (agent_extension_state_role_count() <= 1 && msg->intent == INTENT_IMPLEMENT) {
        prometheus_state_t p_state;
        if (prometheus_check_needs_interview(msg->content, &p_state) == 0 &&
            p_state.needs_interview) {
            pr_info("Prometheus: interview mode, asking questions");
            *out_final_text = strdup(p_state.questions);
            return 0;
        }
    }
#endif
    return ERR_FAIL;
}

static agent_extension_hooks_t ext = {
    .name = "prometheus",
    .replace_run = replace_run,
    .enabled = true,
};

static int __init interview_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

static void __exit interview_module_exit(void)
{
}

module_init(interview_module_init);
module_exit(interview_module_exit);
