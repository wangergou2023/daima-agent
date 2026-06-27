#include "drivers/tool/tool_delegate_dispatch.h"

#include <string.h>

#include "drivers/tool/tool_delegate_background.h"
#include "drivers/tool/tool_delegate_dependency.h"
#include "drivers/tool/tool_delegate_snapshot.h"
#include "drivers/tool/tool_delegate_subagent.h"
#include "kernel/turn/turn_common.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"

static int delegate_background_concurrency_limit(void)
{
    int fallback = DELEGATE_TASK_STORE_MAX;
    int limit = agent_env_int_or_default("DELEGATE_BG_MAX_CONCURRENCY", fallback);

    if (limit <= 0) {
        limit = 1;
    }
    if (limit > DELEGATE_TASK_STORE_MAX) {
        limit = DELEGATE_TASK_STORE_MAX;
    }
    return limit;
}

static bool delegate_task_store_snapshot_quiet(const char *task_id,
                                               delegate_task_record_t *out)
{
    return delegate_task_store_snapshot(task_id, out) == 0;
}

err_t tool_delegate_launch_ready_background_subagents(const char *coordinator_id,
                                                      const char *parent_chat_id)
{
    delegate_coordinator_record_t *record = kzalloc(sizeof(*record), GFP_KERNEL);
    int running_budget = delegate_background_concurrency_limit();
    int running_now = delegate_task_store_running_count();
    if (!record) {
        return ERR_NO_MEM;
    }

    err_t err = delegate_task_store_snapshot_coordinator(coordinator_id, record);
    if (err != 0) {
        kfree(record);
        return err;
    }

    pr_info("delegate_bg launch scan: coordinator=%s agent_count=%d status=%s queued=%d running=%d completed=%d",
            coordinator_id ? coordinator_id : "-",
            record->agent_count,
            record->status[0] ? record->status : "-",
            record->queued_count,
            record->running_count,
            record->completed_count);
    pr_info("delegate_bg launch budget: coordinator=%s running_now=%d limit=%d",
            coordinator_id ? coordinator_id : "-",
            running_now,
            running_budget);

    for (int i = 0; i < record->agent_count; i++) {
        const delegate_coordinator_agent_view_t *agent = &record->agents[i];
        pr_info("delegate_bg launch candidate: coordinator=%s task_id=%s status=%s depends_on=%s subagent=%s",
                coordinator_id ? coordinator_id : "-",
                agent->task_id[0] ? agent->task_id : "-",
                agent->status[0] ? agent->status : "-",
                agent->depends_on[0] ? agent->depends_on : "-",
                agent->subagent_type[0] ? agent->subagent_type : "-");
        if (strcmp(agent->status, "queued") != 0) {
            continue;
        }
        if (!tool_delegate_coordinator_dependencies_satisfied(record, agent)) {
            continue;
        }
        if (running_now >= running_budget) {
            pr_info("delegate_bg budget hold: coordinator=%s task_id=%s running_now=%d limit=%d",
                    coordinator_id ? coordinator_id : "-",
                    agent->task_id[0] ? agent->task_id : "-",
                    running_now,
                    running_budget);
            break;
        }

        delegate_task_record_t task_snapshot;
        memset(&task_snapshot, 0, sizeof(task_snapshot));
        if (!delegate_task_store_snapshot_quiet(agent->task_id, &task_snapshot)) {
            delegate_task_store_fail(agent->task_id, ERR_NOT_FOUND, "delegate_task: queued task snapshot missing");
            continue;
        }
        pr_info("delegate_bg restore queued child: task_id=%s subagent=%s preflight_tool=%s continue_on_error=%d",
                agent->task_id[0] ? agent->task_id : "-",
                task_snapshot.subagent_type[0] ? task_snapshot.subagent_type : "-",
                task_snapshot.preflight_tool.tool_name[0] ? task_snapshot.preflight_tool.tool_name : "-",
                task_snapshot.preflight_tool.continue_on_error ? 1 : 0);

        delegate_subagent_kind_t kind = tool_delegate_parse_subagent_kind(task_snapshot.subagent_type);
        if (kind == DELEGATE_SUBAGENT_INVALID) {
            delegate_task_store_fail(agent->task_id, ERR_INVALID_ARG, "delegate_task: invalid queued subagent type");
            continue;
        }

        if (!delegate_task_store_claim_queued_task(agent->task_id)) {
            pr_info("delegate_bg skip claimed/nonqueued child: coordinator=%s task_id=%s",
                    coordinator_id ? coordinator_id : "-",
                    agent->task_id[0] ? agent->task_id : "-");
            continue;
        }

        delegate_request_t child_req;
        memset(&child_req, 0, sizeof(child_req));
        strscpy(child_req.description, agent->description, sizeof(child_req.description));
        strscpy(child_req.target_path, agent->scope_path, sizeof(child_req.target_path));
        strscpy(child_req.subagent_type, task_snapshot.subagent_type, sizeof(child_req.subagent_type));
        strscpy(child_req.prompt, task_snapshot.prompt, sizeof(child_req.prompt));
        strscpy(child_req.depends_on, task_snapshot.depends_on, sizeof(child_req.depends_on));
        child_req.preflight_tool = *(const typeof(child_req.preflight_tool) *)&task_snapshot.preflight_tool;
        child_req.run_in_background = true;

        if (tool_delegate_schedule_background_subagent(kind,
                                                       &child_req,
                                                       agent->task_id,
                                                       agent->session_id,
                                                       coordinator_id,
                                                       parent_chat_id) != 0) {
            delegate_task_store_fail(agent->task_id, ERR_FAIL, "delegate_task: failed to start queued background worker");
            continue;
        }
        running_now++;
    }

    kfree(record);
    return 0;
}
