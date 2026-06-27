/* delegate_task coordinator summary helpers */
#include "drivers/tool/tool_delegate_summary.h"

#include <string.h>

#include "delegate/delegate_task_store.h"
#include "drivers/tool/tool_delegate_result_json.h"
#include "text.h"

static bool delegate_task_store_snapshot_quiet(const char *task_id,
                                               delegate_task_record_t *out)
{
    return delegate_task_store_snapshot(task_id, out) == 0;
}

void tool_delegate_render_background_coordinator_summary(const delegate_coordinator_record_t *record,
                                                         char *summary,
                                                         size_t summary_size)
{
    int done_count = 0;
    int running_count = 0;
    int error_count = 0;
    int write_ready_count = 0;
    int listed = 0;

    if (!summary || summary_size == 0) {
        return;
    }
    summary[0] = '\0';
    if (!record) {
        return;
    }

    for (int i = 0; i < record->agent_count; i++) {
        const delegate_coordinator_agent_view_t *agent = &record->agents[i];
        if (strcmp(agent->status, "done") == 0) {
            done_count++;
        } else if (strcmp(agent->status, "error") == 0) {
            error_count++;
        } else {
            running_count++;
        }
        if (agent->write_approved) {
            write_ready_count++;
        }
    }

    snprintf(summary,
             summary_size,
             "coordinator %s：共 %d 个子任务，已完成 %d 个，进行中 %d 个，失败 %d 个。",
             record->coordinator_id,
             record->agent_count,
             done_count,
             running_count,
             error_count);

    if (write_ready_count > 0) {
        char tail[96];
        snprintf(tail, sizeof(tail), " 其中 %d 个 implement 任务已通过写入冲突检查。", write_ready_count);
        strlcat(summary, tail, summary_size);
    }

    for (int i = 0; i < record->agent_count && listed < 4; i++) {
        const delegate_coordinator_agent_view_t *agent = &record->agents[i];
        if (listed == 0) {
            strlcat(summary, "\n\n任务摘要：", summary_size);
        }
        strlcat(summary, "\n- ", summary_size);
        strlcat(summary, agent->subagent_type[0] ? agent->subagent_type : "subagent", summary_size);
        strlcat(summary, " / ", summary_size);
        strlcat(summary, agent->description[0] ? agent->description : agent->task_id, summary_size);
        strlcat(summary, " / ", summary_size);
        strlcat(summary, agent->status[0] ? agent->status : "unknown", summary_size);
        if (strcmp(agent->status, "done") == 0) {
            delegate_task_record_t task_snapshot;
            char preview[200];
            memset(&task_snapshot, 0, sizeof(task_snapshot));
            if (delegate_task_store_snapshot_quiet(agent->task_id, &task_snapshot) &&
                task_snapshot.output[0]) {
                if (!tool_delegate_parse_result_json_rendered(task_snapshot.output,
                                                              preview,
                                                              sizeof(preview))) {
                    text_shorten(task_snapshot.output, preview, sizeof(preview), 120);
                }
                strlcat(summary, " / ", summary_size);
                strlcat(summary, preview, summary_size);
            }
        }
        listed++;
    }
}
