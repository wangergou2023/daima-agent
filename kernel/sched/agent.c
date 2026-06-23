/* Agent 生命周期管理：init → launch（阻塞 LLM 调用）→ status check → reap。
 * sched_agent_launch() 是同步阻塞调用，直接发起 LLM 请求并等待完整回复。 */

#include "sched.h"
#include "autoconf.h"
#include "router.h"
#include "linux/printk.h"
#include "drivers/llm/llm_proxy.h"
#include "linux/slab.h"
#include <stdio.h>
#include <string.h>
#include "linux/kernel.h"

static agent_role_t sched_class_to_role(enum sched_class_id class_id)
{
    switch (class_id) {
    case SCHED_CLASS_PLANNER:
        return AGENT_ROLE_PLANNER;
    case SCHED_CLASS_EXECUTOR:
        return AGENT_ROLE_EXECUTOR;
    case SCHED_CLASS_REVIEWER:
        return AGENT_ROLE_REVIEWER;
    default:
        return AGENT_ROLE_FAST;
    }
}

static const char *sched_agent_model_for_role(const struct sched_agent *agent)
{
    if (!agent) {
        return llm_get_model_name();
    }

    const category_profile_t *profile =
        category_router_resolve_for_role(sched_class_to_role(agent->class));
    if (profile && profile->model[0]) {
        return profile->model;
    }
    return llm_get_model_name();
}

/** 初始化 agent：清零 → 设置 pid/class/state → 拷贝 prompt 后缀和任务描述。 */
void sched_agent_init(struct sched_agent *agent, const struct sched_class *cls,
                      const char *task)
{
    if (!agent || !cls) return;
    memset(agent, 0, sizeof(*agent));
    agent->pid = -1;
    agent->class = (enum sched_class_id)cls->priority;
    agent->state = SCHED_AGENT_WAITING;
    agent->error = 0;
    strscpy(agent->prompt_add, cls->prompt_suffix, sizeof(agent->prompt_add));
    strscpy(agent->task_desc, task ? task : "", sizeof(agent->task_desc));
}

/** 启动 agent：发起同步 LLM 调用，等待回复后设置 DONE/ERROR 状态。
 *  这是阻塞操作——agent 在 LLM 回复返回前不会返回。 */
void sched_agent_launch(struct sched_agent *agent, const char *prompt,
                        cJSON *messages, const char *tools)
{
    if (!agent || agent->state != SCHED_AGENT_WAITING) return;

    char safe[2048];
    if (prompt) {
        const char *s = prompt; char *d = safe;
        while (*s && (size_t)(d - safe) < sizeof(safe) - 1) {
            char ch = *s++;
            *d++ = (ch == '\n' || ch == '\r') ? ' ' : ch;
        }
        *d = '\0';
    } else {
        safe[0] = '\0';
    }

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    const char *model_name = sched_agent_model_for_role(agent);
    agent->error = llm_chat_tools_with_model(safe[0] ? safe : "ok", messages, tools,
                                              model_name, &resp);
    if (agent->error == 0 && resp.text) {
        strscpy(agent->result, resp.text, sizeof(agent->result));
        agent->state = SCHED_AGENT_DONE;
    } else {
        agent->state = SCHED_AGENT_ERROR;
    }
    llm_response_free(&resp);
    pr_info("agent %d (%s) done, model=%s, err=%d", agent->pid,
            sched_class_name(agent->class),
            model_name ? model_name : "(null)",
            agent->error);
}

/** 检查 agent 是否已完成（DONE 或 ERROR 状态）。 */
bool sched_agent_is_done(struct sched_agent *agent)
{
    return agent && (agent->state == SCHED_AGENT_DONE || agent->state == SCHED_AGENT_ERROR);
}

/** 回收 agent 资源（当前为空操作，预留扩展接口）。 */
void sched_agent_reap(struct sched_agent *agent)
{
    (void)agent;
}
