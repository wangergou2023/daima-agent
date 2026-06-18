/* 调度器核心实现：runqueue 管理、agent 调度（dispatch/start/wait/merge/exit）。
 * sched_dispatch() 根据 intent 创建对应调度类的 agent 队列，
 * sched_start() 按优先级顺序启动 agent，sched_wait() 轮询等待完成，
 * sched_merge() 合并多 agent 输出为最终回复。 */

#include "sched.h"
#include "autoconf.h"
#include "linux/compiler.h"
#include "linux/printk.h"
#include "os.h"

#include <stdio.h>
#include <string.h>
#include "linux/kernel.h"
/** 检查 runqueue 的链表是否已初始化。 */
static bool sched_runqueue_has_list(const struct sched_runqueue *rq)
{
    return rq && rq->agent_list.next && rq->agent_list.prev;
}

void sched_init(void)
{
}

/** 为 agent 设置任务描述：[调度类名] 用户任务 + 计划文本。 */
static void sched_set_task_description(struct sched_agent *agent,
                                       const struct plan *plan,
                                       const char *user_msg)
{
    const char *message = user_msg ? user_msg : "";
    const char *plan_text = (plan && plan->has_plan) ? plan->plan_text : "";
    size_t used = 0;

    used = (size_t)snprintf(agent->task_desc,
                            sizeof(agent->task_desc),
                            "[%s] 用户任务：",
                            sched_class_name(agent->class));
    if (used >= sizeof(agent->task_desc)) {
        return;
    }

    strncat(agent->task_desc, message, sizeof(agent->task_desc) - used - 1);
    used = strlen(agent->task_desc);

    if (plan_text[0] != '\0' && used < sizeof(agent->task_desc) - 1) {
        strncat(agent->task_desc, "\n计划：", sizeof(agent->task_desc) - used - 1);
        used = strlen(agent->task_desc);
        strncat(agent->task_desc, plan_text, sizeof(agent->task_desc) - used - 1);
    }
}

/** 将新 agent 入队：初始化 → 设置 pid → 加入链表 → nr_agents++。 */
void sched_enqueue(struct sched_runqueue *rq, const struct sched_class *cls,
                   const char *task)
{
    if (unlikely(!rq || !cls || rq->nr_agents >= SCHED_MAX_AGENTS)) {
        return;
    }

    struct sched_agent *agent = &rq->agents[rq->nr_agents];
    sched_agent_init(agent, cls, task);
    agent->pid = rq->nr_agents;
    list_add(&agent->run_list, &rq->agent_list);
    rq->nr_agents++;
}

/** 将运行中的 agent 标记为 DONE 并减少运行计数。 */
void sched_dequeue(struct sched_runqueue *rq, struct sched_agent *agent)
{
    if (unlikely(!rq || !agent || agent->state != SCHED_AGENT_RUNNING)) {
        return;
    }
    agent->state = SCHED_AGENT_DONE;
    if (rq->nr_running > 0) {
        rq->nr_running--;
    }
}

/** 从队列中选出下一个 WAITING 状态且 class 优先级最高的 agent。
 *  调度类 ID 越小优先级越高（PLANNER=0 > EXECUTOR=1 > REVIEWER=2）。 */
struct sched_agent *sched_pick_next(struct sched_runqueue *rq)
{
    struct sched_agent *best = NULL;

    if (unlikely(!rq)) {
        return NULL;
    }

    struct sched_agent *agent;
    list_for_each_entry(agent, &rq->agent_list, run_list, struct sched_agent) {
        if (agent->state != SCHED_AGENT_WAITING) {
            continue;
        }
        if (unlikely(!best) || agent->class < best->class) {
            best = agent;
        }
    }
    return best;
}

/** 完成 agent 执行：根据错误码设置最终状态（DONE/TIMEOUT/ERROR），更新 runqueue 计数。
 *  @param err  0=成功，ERR_TIMEOUT=超时，其他=错误 */
void sched_complete(struct sched_runqueue *rq, struct sched_agent *agent,
                    err_t err)
{
    if (unlikely(!rq || !agent)) {
        return;
    }

    agent->error = err;
    if (likely(err == 0)) {
        agent->state = SCHED_AGENT_DONE;
    } else if (err == ERR_TIMEOUT) {
        agent->state = SCHED_AGENT_TIMEOUT;
    } else {
        agent->state = SCHED_AGENT_ERROR;
    }
    if (rq->nr_running > 0) {
        rq->nr_running--;
    }
}

/** 调度入口：根据 intent 决定需要哪些调度类 → 创建 runqueue 并填充 agent。
 *  IMPLEMENT → PLANNER+EXECUTOR+REVIEWER，FIX → EXECUTOR+REVIEWER，其他 → 单 EXECUTOR。
 *  @return 0 成功，ERR_INVALID_ARG 失败 */
err_t sched_dispatch(enum intent intent, const struct plan *plan,
                           const char *user_msg, struct sched_runqueue *rq)
{
    if (unlikely(!rq)) {
        return ERR_INVALID_ARG;
    }

    memset(rq, 0, sizeof(*rq));
    INIT_LIST_HEAD(&rq->agent_list);
    rq->timeout_ms = 0;

    int count = 0;
    const struct sched_class *classes = sched_class_for_intent(intent, &count);
    if (!classes || count <= 0 || count > SCHED_MAX_AGENTS) {
        return ERR_INVALID_ARG;
    }

    for (int i = 0; i < count; i++) {
        sched_enqueue(rq, &classes[i], "");
        sched_set_task_description(&rq->agents[i], plan, user_msg);
    }

    return 0;
}

/** 启动调度：按优先级顺序逐个 pick → launch agent。
 *  每个 agent 获得拼接了 prompt_suffix 和 task_desc 的独立 system prompt。 */
void sched_start(struct sched_runqueue *rq,
                 const char *system_prompt, cJSON *messages, const char *tools)
{
    if (unlikely(!rq)) {
        return;
    }

    const char *base_prompt = system_prompt ? system_prompt : "";
    struct sched_agent *agent = NULL;
    while ((agent = sched_pick_next(rq)) != NULL) {
        char scoped_prompt[CONTEXT_BUF_SIZE];
        int written = snprintf(scoped_prompt,
                               sizeof(scoped_prompt),
                               "%s\n%s\n\n## 子任务\n%s",
                               base_prompt,
                               agent->prompt_add,
                               agent->task_desc);
        if (written < 0 || (size_t)written >= sizeof(scoped_prompt)) {
            scoped_prompt[sizeof(scoped_prompt) - 1] = '\0';
        }

        sched_agent_launch(agent, scoped_prompt, messages, tools);
        if (agent->state == SCHED_AGENT_RUNNING) {
            rq->nr_running++;
        }
    }
}

/** 等待所有 agent 完成：轮询 checklist → reap → complete，每 100ms 检查一次。
 *  超时后未完成的 agent 强制标记为 TIMEOUT。 */
err_t sched_wait(struct sched_runqueue *rq)
{
    if (unlikely(!rq)) {
        return ERR_INVALID_ARG;
    }

    int elapsed = 0;
    const int timeout_ms = rq->timeout_ms;
    const int poll_interval_ms = 100;
    int last_progress_sec = 0;
    struct sched_agent *agent;

    while (rq->nr_running > 0 && elapsed < timeout_ms) {
        list_for_each_entry(agent, &rq->agent_list, run_list, struct sched_agent) {
            if (!sched_agent_is_done(agent)) {
                continue;
            }

            sched_agent_reap(agent);
            err_t err = agent->error;
            enum sched_agent_state state = agent->state;
            sched_complete(rq, agent, err);
            agent->state = state;
            pr_info("agent %d (%s) done, err=%s", agent->pid, sched_class_name(agent->class), err_name(agent->error));
        }

        if (rq->nr_running > 0) {
            task_delay((uint32_t)poll_interval_ms);
            elapsed += poll_interval_ms;
            int elapsed_sec = elapsed / 1000;
            if (elapsed_sec > 0 && elapsed_sec % 10 == 0 && elapsed_sec != last_progress_sec) {
                last_progress_sec = elapsed_sec;
                pr_info("waiting... %ds elapsed, %d agents running", elapsed_sec, rq->nr_running);
            }
        }
    }

    list_for_each_entry(agent, &rq->agent_list, run_list, struct sched_agent) {
        if (agent->state == SCHED_AGENT_RUNNING) {
            sched_complete(rq, agent, ERR_TIMEOUT);
            pr_warn("agent %d (%s) timed out", agent->pid, sched_class_name(agent->class));
        }
    }

    return 0;
}

/** 合并多 agent 输出：收集各 agent 状态摘要 + EXECUTOR 结果 + REVIEWER 审查。
 *  PLANNER 输出不展示给用户（内部计划用），REVIEWER 通过时仅显示审查通过。 */
void sched_merge(struct sched_runqueue *rq, char *output, size_t size)
{
    if (unlikely(!rq || !output || size == 0)) {
        return;
    }

    if (!sched_runqueue_has_list(rq)) {
        INIT_LIST_HEAD(&rq->agent_list);
        for (int i = 0; i < rq->nr_agents && i < SCHED_MAX_AGENTS; i++) {
            INIT_LIST_HEAD(&rq->agents[i].run_list);
            rq->agents[i].pid = i;
            list_add(&rq->agents[i].run_list, &rq->agent_list);
        }
    }

    output[0] = '\0';
    rq->merged[0] = '\0';

#define SCHED_DISPLAY_MAX 2000

    size_t used = 0;
    struct sched_agent *agent;
    list_for_each_entry(agent, &rq->agent_list, run_list, struct sched_agent) {
        if (used >= size) break;
        if (agent->state != SCHED_AGENT_DONE && agent->state != SCHED_AGENT_ERROR &&
            agent->state != SCHED_AGENT_TIMEOUT) {
            continue;
        }
        if (agent->class == SCHED_CLASS_PLANNER) {
            continue;
        }

        const char *icon = agent->error == 0 ? "✅" : "❌";
        int written = snprintf(output + used, size - used,
                               "%s %s %s\n",
                               icon,
                               sched_class_name(agent->class),
                               agent->error == 0 ? "已完成" :
                               agent->error == ERR_TIMEOUT ? "超时" : "失败");
        if (written < 0) {
            return;
        }
        used += (size_t)written;
    }

    list_for_each_entry(agent, &rq->agent_list, run_list, struct sched_agent) {
        if (used >= size) break;
        if (agent->state == SCHED_AGENT_DONE || agent->state == SCHED_AGENT_ERROR ||
            agent->state == SCHED_AGENT_TIMEOUT) {
            continue;
        }
        int written = snprintf(output + used, size - used,
                               "⏱️ %s 未完成 (超时)\n",
                               sched_class_name(agent->class));
        if (written < 0) {
            return;
        }
        used += (size_t)written;
    }

    if (used < size) {
        int written = snprintf(output + used, size - used, "\n---\n\n");
        if (written < 0) {
            return;
        }
        used += (size_t)written;
    }

    const struct sched_agent *executor = NULL;
    const struct sched_agent *reviewer = NULL;
    list_for_each_entry(agent, &rq->agent_list, run_list, struct sched_agent) {
        if (agent->state != SCHED_AGENT_DONE) {
            continue;
        }
        if (agent->class == SCHED_CLASS_EXECUTOR) executor = agent;
        if (agent->class == SCHED_CLASS_REVIEWER) reviewer = agent;
    }

    if (executor && executor->result[0] && used < size) {
        size_t elen = strlen(executor->result);
        int written;
        if (elen > SCHED_DISPLAY_MAX) {
            written = snprintf(output + used, size - used,
                               "%.2000s\n\n...(输出过长已截断, 查看文件获取完整内容)",
                               executor->result);
        } else {
            written = strscpy(output + used, executor->result, size - used);
        }
        if (written < 0) {
            return;
        }
        used += (size_t)written;
    }

    if (reviewer && reviewer->result[0] &&
        strstr(reviewer->result, "审查通过") == NULL &&
        strstr(reviewer->result, "已完成") == NULL && used < size) {
        snprintf(output + used, size - used,
                 "\n\n---\n⚠️ REVIEWER: %s", reviewer->result);
    }

    strscpy(rq->merged, output, sizeof(rq->merged));
}

/** 清理 runqueue：释放所有 agent 的 async LLM 会话、消息、响应，并清零整个结构。 */
void sched_exit(struct sched_runqueue *rq)
{
    if (unlikely(!rq)) {
        return;
    }
    struct sched_agent *agent;
    list_for_each_entry(agent, &rq->agent_list, run_list, struct sched_agent) {
        if (agent->async_chat) {
            llm_chat_async_free(agent->async_chat);
            agent->async_chat = NULL;
        }
        if (agent->scoped_messages) {
            cJSON_Delete(agent->scoped_messages);
            agent->scoped_messages = NULL;
        }
        llm_response_free(&agent->response);
    }
    memset(rq, 0, sizeof(*rq));
}
