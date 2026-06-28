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

static int delegate_background_per_coordinator_limit(void)
{
    int fallback = DELEGATE_COORDINATOR_AGENTS_MAX;
    int limit = agent_env_int_or_default("DELEGATE_BG_MAX_PER_COORDINATOR", fallback);

    if (limit <= 0) {
        limit = 1;
    }
    if (limit > DELEGATE_COORDINATOR_AGENTS_MAX) {
        limit = DELEGATE_COORDINATOR_AGENTS_MAX;
    }
    return limit;
}

static int delegate_background_per_parent_limit(void)
{
    int fallback = DELEGATE_TASK_STORE_MAX;
    int limit = agent_env_int_or_default("DELEGATE_BG_MAX_PER_PARENT", fallback);

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
    int running_budget = delegate_background_concurrency_limit();
    int per_coordinator_limit = delegate_background_per_coordinator_limit();
    int per_parent_limit = delegate_background_per_parent_limit();
    int scan_round = 0;

    while (1) {
        delegate_coordinator_record_t *record = kzalloc(sizeof(*record), GFP_KERNEL);
        int running_now = delegate_task_store_running_count();
        bool launched_any = false;
        bool saw_schedulable_candidate = false;
        err_t err;

        if (!record) {
            return ERR_NO_MEM;
        }

        err = delegate_task_store_snapshot_coordinator(coordinator_id, record);
        if (err != 0) {
            kfree(record);
            return err;
        }

        pr_info("delegate_bg launch scan: coordinator=%s round=%d agent_count=%d status=%s queued=%d running=%d completed=%d",
                coordinator_id ? coordinator_id : "-",
                scan_round,
                record->agent_count,
                record->status[0] ? record->status : "-",
                record->queued_count,
                record->running_count,
                record->completed_count);
        pr_info("delegate_bg launch budget: coordinator=%s round=%d running_now=%d limit=%d",
                coordinator_id ? coordinator_id : "-",
                scan_round,
                running_now,
                running_budget);
        pr_info("delegate_bg launch coordinator cap: coordinator=%s round=%d coordinator_running=%d limit=%d",
                coordinator_id ? coordinator_id : "-",
                scan_round,
                record->running_count,
                per_coordinator_limit);
        if (parent_chat_id && parent_chat_id[0]) {
            int parent_running = delegate_task_store_running_count_for_parent(parent_chat_id);
            pr_info("delegate_bg launch parent cap: coordinator=%s round=%d parent_chat=%s running=%d limit=%d",
                    coordinator_id ? coordinator_id : "-",
                    scan_round,
                    parent_chat_id,
                    parent_running,
                    per_parent_limit);
            if (parent_running >= per_parent_limit) {
                pr_info("delegate_bg parent cap hold: coordinator=%s parent_chat=%s running=%d limit=%d",
                        coordinator_id ? coordinator_id : "-",
                        parent_chat_id,
                        parent_running,
                        per_parent_limit);
                kfree(record);
                return 0;
            }
        }

        if (record->running_count >= per_coordinator_limit) {
            pr_info("delegate_bg coordinator cap hold: coordinator=%s running=%d limit=%d",
                    coordinator_id ? coordinator_id : "-",
                    record->running_count,
                    per_coordinator_limit);
            kfree(record);
            return 0;
        }

        for (int i = 0; i < record->agent_count; i++) {
            const delegate_coordinator_agent_view_t *agent = &record->agents[i];
            pr_info("delegate_bg launch candidate: coordinator=%s round=%d idx=%d task_id=%s status=%s depends_on=%s subagent=%s",
                    coordinator_id ? coordinator_id : "-",
                    scan_round,
                    i,
                    agent->task_id[0] ? agent->task_id : "-",
                    agent->status[0] ? agent->status : "-",
                    agent->depends_on[0] ? agent->depends_on : "-",
                    agent->subagent_type[0] ? agent->subagent_type : "-");
            if (strcmp(agent->status, "queued") != 0) {
                pr_info("delegate_bg skip nonqueued child: coordinator=%s task_id=%s status=%s",
                        coordinator_id ? coordinator_id : "-",
                        agent->task_id[0] ? agent->task_id : "-",
                        agent->status[0] ? agent->status : "-");
                continue;
            }
            pr_info("delegate_bg queued child passed status gate: coordinator=%s task_id=%s",
                    coordinator_id ? coordinator_id : "-",
                    agent->task_id[0] ? agent->task_id : "-");
            if (!tool_delegate_coordinator_dependencies_satisfied(record, agent)) {
                pr_info("delegate_bg dependency hold: coordinator=%s task_id=%s depends_on=%s",
                        coordinator_id ? coordinator_id : "-",
                        agent->task_id[0] ? agent->task_id : "-",
                        agent->depends_on[0] ? agent->depends_on : "-");
                continue;
            }
            pr_info("delegate_bg queued child passed dependency gate: coordinator=%s task_id=%s",
                    coordinator_id ? coordinator_id : "-",
                    agent->task_id[0] ? agent->task_id : "-");
            saw_schedulable_candidate = true;
            if (running_now >= running_budget) {
                pr_info("delegate_bg budget hold: coordinator=%s task_id=%s running_now=%d limit=%d",
                        coordinator_id ? coordinator_id : "-",
                        agent->task_id[0] ? agent->task_id : "-",
                        running_now,
                        running_budget);
                break;
            }
            pr_info("delegate_bg queued child passed budget gate: coordinator=%s task_id=%s",
                    coordinator_id ? coordinator_id : "-",
                    agent->task_id[0] ? agent->task_id : "-");

            delegate_task_record_t task_snapshot;
            memset(&task_snapshot, 0, sizeof(task_snapshot));
            if (!delegate_task_store_snapshot_quiet(agent->task_id, &task_snapshot)) {
                pr_warn("delegate_bg queued snapshot missing: coordinator=%s task_id=%s",
                        coordinator_id ? coordinator_id : "-",
                        agent->task_id[0] ? agent->task_id : "-");
                delegate_task_store_fail(agent->task_id, ERR_NOT_FOUND, "delegate_task: queued task snapshot missing");
                continue;
            }
            pr_info("delegate_bg queued child snapshot loaded: coordinator=%s task_id=%s snapshot_subagent=%s",
                    coordinator_id ? coordinator_id : "-",
                    agent->task_id[0] ? agent->task_id : "-",
                    task_snapshot.subagent_type[0] ? task_snapshot.subagent_type : "-");
            pr_info("delegate_bg restore queued child: task_id=%s subagent=%s preflight_tool=%s continue_on_error=%d",
                    agent->task_id[0] ? agent->task_id : "-",
                    task_snapshot.subagent_type[0] ? task_snapshot.subagent_type : "-",
                    task_snapshot.preflight_tool.tool_name[0] ? task_snapshot.preflight_tool.tool_name : "-",
                    task_snapshot.preflight_tool.continue_on_error ? 1 : 0);

            delegate_subagent_kind_t kind = tool_delegate_parse_subagent_kind(task_snapshot.subagent_type);
            if (kind == DELEGATE_SUBAGENT_INVALID) {
                pr_warn("delegate_bg invalid queued child kind: coordinator=%s task_id=%s subagent=%s",
                        coordinator_id ? coordinator_id : "-",
                        agent->task_id[0] ? agent->task_id : "-",
                        task_snapshot.subagent_type[0] ? task_snapshot.subagent_type : "-");
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
                pr_warn("delegate_bg schedule failed: coordinator=%s task_id=%s session_id=%s",
                        coordinator_id ? coordinator_id : "-",
                        agent->task_id[0] ? agent->task_id : "-",
                        agent->session_id[0] ? agent->session_id : "-");
                delegate_task_store_fail(agent->task_id, ERR_FAIL, "delegate_task: failed to start queued background worker");
                continue;
            }
            launched_any = true;
            running_now++;
            record->running_count++;
            break;
        }

        kfree(record);

        if (!launched_any) {
            if (!saw_schedulable_candidate) {
                return 0;
            }
            if (running_now >= running_budget) {
                return 0;
            }
            return 0;
        }
        scan_round++;
    }

    return 0;
}

err_t tool_delegate_launch_one_ready_background_subagent(const char *coordinator_id,
                                                         const char *parent_chat_id)
{
    int running_budget = delegate_background_concurrency_limit();
    int per_coordinator_limit = delegate_background_per_coordinator_limit();
    int per_parent_limit = delegate_background_per_parent_limit();
    delegate_coordinator_record_t *record = kzalloc(sizeof(*record), GFP_KERNEL);
    int running_now = delegate_task_store_running_count();
    err_t err;

    if (!record) {
        return ERR_NO_MEM;
    }

    err = delegate_task_store_snapshot_coordinator(coordinator_id, record);
    if (err != 0) {
        kfree(record);
        return err;
    }

    pr_info("delegate_bg fair launch scan: coordinator=%s agent_count=%d status=%s queued=%d running=%d completed=%d",
            coordinator_id ? coordinator_id : "-",
            record->agent_count,
            record->status[0] ? record->status : "-",
            record->queued_count,
            record->running_count,
            record->completed_count);
    pr_info("delegate_bg fair launch budget: coordinator=%s running_now=%d limit=%d",
            coordinator_id ? coordinator_id : "-",
            running_now,
            running_budget);
    pr_info("delegate_bg fair launch coordinator cap: coordinator=%s running=%d limit=%d",
            coordinator_id ? coordinator_id : "-",
            record->running_count,
            per_coordinator_limit);
    if (parent_chat_id && parent_chat_id[0]) {
        int parent_running = delegate_task_store_running_count_for_parent(parent_chat_id);
        pr_info("delegate_bg fair launch parent cap: coordinator=%s parent_chat=%s running=%d limit=%d",
                coordinator_id ? coordinator_id : "-",
                parent_chat_id,
                parent_running,
                per_parent_limit);
        if (parent_running >= per_parent_limit) {
            pr_info("delegate_bg fair launch parent cap hold: coordinator=%s parent_chat=%s running=%d limit=%d",
                    coordinator_id ? coordinator_id : "-",
                    parent_chat_id,
                    parent_running,
                    per_parent_limit);
            kfree(record);
            return 0;
        }
    }

    if (record->running_count >= per_coordinator_limit) {
        pr_info("delegate_bg fair launch cap hold: coordinator=%s running=%d limit=%d",
                coordinator_id ? coordinator_id : "-",
                record->running_count,
                per_coordinator_limit);
        kfree(record);
        return 0;
    }

    for (int i = 0; i < record->agent_count; i++) {
        const delegate_coordinator_agent_view_t *agent = &record->agents[i];

        if (strcmp(agent->status, "queued") != 0) {
            continue;
        }
        if (!tool_delegate_coordinator_dependencies_satisfied(record, agent)) {
            continue;
        }
        if (running_now >= running_budget) {
            kfree(record);
            return 0;
        }

        delegate_task_record_t task_snapshot;
        memset(&task_snapshot, 0, sizeof(task_snapshot));
        if (!delegate_task_store_snapshot_quiet(agent->task_id, &task_snapshot)) {
            delegate_task_store_fail(agent->task_id, ERR_NOT_FOUND, "delegate_task: queued task snapshot missing");
            continue;
        }

        delegate_subagent_kind_t kind = tool_delegate_parse_subagent_kind(task_snapshot.subagent_type);
        if (kind == DELEGATE_SUBAGENT_INVALID) {
            delegate_task_store_fail(agent->task_id, ERR_INVALID_ARG, "delegate_task: invalid queued subagent type");
            continue;
        }

        if (!delegate_task_store_claim_queued_task(agent->task_id)) {
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

        pr_info("delegate_bg fair launch picked: coordinator=%s task_id=%s",
                coordinator_id ? coordinator_id : "-",
                agent->task_id[0] ? agent->task_id : "-");
        kfree(record);
        return 0;
    }

    kfree(record);
    return 0;
}
