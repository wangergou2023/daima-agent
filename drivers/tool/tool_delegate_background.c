#include "drivers/tool/tool_delegate_background.h"

#include <string.h>

#include "drivers/tool/tool_delegate_dispatch.h"
#include "drivers/tool/tool_delegate_protocol.h"
#include "drivers/tool/tool_delegate_sync.h"
#include "drivers/tool/tool_delegate_snapshot.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "os.h"

#define DELEGATE_RESULT_JSON_MAX 3072

typedef struct {
    delegate_subagent_kind_t kind;
    delegate_request_t req;
    char task_id[DELEGATE_TASK_ID_LEN];
    char session_id[32];
    char parent_chat_id[64];
} background_subagent_job_t;

static void extract_target_files_from_summary_text(const char *text,
                                                   char *target_files,
                                                   size_t target_files_size)
{
    const char *marker;
    size_t i = 0;

    if (!target_files || target_files_size == 0) {
        return;
    }
    target_files[0] = '\0';
    if (!text || !text[0]) {
        return;
    }

    marker = strstr(text, "target_files:");
    if (!marker) {
        return;
    }
    marker += strlen("target_files:");
    while (*marker == ' ' || *marker == '\n' || *marker == '\t') {
        marker++;
    }
    while (marker[i] && marker[i] != '\n' && i + 1 < target_files_size) {
        target_files[i] = marker[i];
        i++;
    }
    target_files[i] = '\0';
}

static void background_subagent_task(void *arg)
{
    background_subagent_job_t *job = (background_subagent_job_t *)arg;
    char output[DELEGATE_RESULT_JSON_MAX * 2];
    char summary[DELEGATE_TASK_OUTPUT_LEN];
    char target_files[DELEGATE_TASK_FILESET_LEN];
    bool write_approved = false;
    err_t err;

    if (!job) {
        return;
    }

    memset(output, 0, sizeof(output));
    memset(summary, 0, sizeof(summary));
    memset(target_files, 0, sizeof(target_files));

    pr_info("delegate_bg worker wait: task_id=%s subagent=%s description=%s",
            job->task_id,
            job->req.subagent_type[0] ? job->req.subagent_type : "-",
            job->req.description[0] ? job->req.description : "-");
    pr_info("delegate_bg worker start: task_id=%s subagent=%s parent_chat=%s",
            job->task_id,
            job->req.subagent_type[0] ? job->req.subagent_type : "-",
            job->parent_chat_id[0] ? job->parent_chat_id : "-");
    err = tool_delegate_run_sync_single_subagent(job->kind,
                                                 &job->req,
                                                 job->task_id,
                                                 job->session_id,
                                                 job->req.coordinator_id,
                                                 job->parent_chat_id,
                                                 output,
                                                 sizeof(output));
    pr_info("delegate_bg worker finish: task_id=%s err=%s",
            job->task_id,
            err_name(err));
    if (err != 0) {
        delegate_task_store_fail(job->task_id, err, output);
        if (job->req.coordinator_id[0]) {
            tool_delegate_launch_ready_background_subagents(job->req.coordinator_id,
                                                            job->parent_chat_id);
        }
        kfree(job);
        return;
    }

    if (!tool_delegate_extract_sync_final_output(output, summary, sizeof(summary))) {
        strscpy(summary, output, sizeof(summary));
    }
    extract_target_files_from_summary_text(summary, target_files, sizeof(target_files));
    write_approved = target_files[0] != '\0';
    delegate_task_store_complete(job->task_id, summary, target_files, write_approved);
    if (job->req.coordinator_id[0]) {
        tool_delegate_launch_ready_background_subagents(job->req.coordinator_id,
                                                        job->parent_chat_id);
    }
    kfree(job);
}

err_t tool_delegate_schedule_background_subagent(delegate_subagent_kind_t kind,
                                                 const delegate_request_t *req,
                                                 const char *task_id,
                                                 const char *session_id,
                                                 const char *coordinator_id,
                                                 const char *parent_chat_id)
{
    background_subagent_job_t *job;

    if (!req || !task_id || !task_id[0] || !session_id || !session_id[0]) {
        return ERR_INVALID_ARG;
    }

    job = kzalloc(sizeof(*job), GFP_KERNEL);
    if (!job) {
        return ERR_NO_MEM;
    }

    job->kind = kind;
    memcpy(&job->req, req, sizeof(*req));
    strscpy(job->req.coordinator_id, coordinator_id ? coordinator_id : "", sizeof(job->req.coordinator_id));
    strscpy(job->task_id, task_id, sizeof(job->task_id));
    strscpy(job->session_id, session_id, sizeof(job->session_id));
    strscpy(job->parent_chat_id, parent_chat_id ? parent_chat_id : "", sizeof(job->parent_chat_id));

    if (!task_create(background_subagent_task, "delegate_bg", 32768, job, 3, NULL)) {
        kfree(job);
        return ERR_FAIL;
    }
    return 0;
}
