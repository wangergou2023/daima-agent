#include "agent/agent_turn_finish.h"

#include <stdio.h>
#include <stdlib.h>

#include "agent/agent_turn_common.h"
#include "agent/agent_turn_persist.h"
#include "agent/compaction_recovery.h"
#include "agent/session_recovery.h"
#include "agent/todo_enforcer.h"
#include "app/daima_paths.h"
#include "cJSON.h"
#include "daima_config.h"
#include "daima_log.h"
#include "daima_os.h"
#include "daima_platform.h"
#if DAIMA_SKILL_SCOPED_TOOLS_ENABLED
#include "skills/skill_tools.h"
#endif

static const char *TAG = "agent_finish";

#ifdef DAIMA_TODO_ENFORCER_ENABLED
static void read_todo_counts(int *out_total, int *out_completed)
{
    *out_total = 0;
    *out_completed = 0;

    FILE *f = fopen(daima_path_todo_file(), "r");
    if (!f) {
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 128 * 1024) {
        fclose(f);
        return;
    }

    char *buf = calloc(1, (size_t)size + 1);
    if (!buf) {
        fclose(f);
        return;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *items = cJSON_GetObjectItem(root, "items");
    if (items && cJSON_IsArray(items)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, items) {
            (*out_total)++;
            cJSON *done = cJSON_GetObjectItem(item, "done");
            if (cJSON_IsTrue(done) || (cJSON_IsNumber(done) && done->valueint != 0)) {
                (*out_completed)++;
            }
        }
    }
    cJSON_Delete(root);
}
#endif

void agent_turn_finish(
    daima_msg_t *msg,
    char **io_final_text,
    char **io_reasoning_text,
    daima_err_t turn_err,
    int iteration,
    bool tool_budget_exhausted,
    bool cancelled)
{
    char *final_text = io_final_text ? *io_final_text : NULL;
    char *reasoning_text = io_reasoning_text ? *io_reasoning_text : NULL;

    if (cancelled) {
        free(final_text);
        free(reasoning_text);
        final_text = NULL;
        reasoning_text = NULL;
        if (io_final_text) {
            *io_final_text = NULL;
        }
        if (io_reasoning_text) {
            *io_reasoning_text = NULL;
        }
        DAIMA_LOGI(TAG, "Skip final response for cancelled turn %s:%s",
                   msg ? msg->channel : "-",
                   msg ? msg->chat_id : "-");
#if DAIMA_SKILL_SCOPED_TOOLS_ENABLED
        skill_tools_unregister_all();
#endif
        agent_cleanup_inbound_msg(msg);
        return;
    }

    if (final_text && final_text[0]) {
        agent_turn_save_session(msg, final_text, reasoning_text, iteration);
        agent_turn_queue_outbound_text(msg, final_text, reasoning_text, true);
        final_text = NULL;
        free(reasoning_text);
        reasoning_text = NULL;
    } else {
        free(final_text);
        free(reasoning_text);
        reasoning_text = NULL;
        final_text = agent_turn_build_error_reply(tool_budget_exhausted);
        if (final_text) {
            agent_turn_queue_outbound_text(msg, final_text, NULL, true);
            final_text = NULL;
        }
    }

    if (io_final_text) {
        *io_final_text = final_text;
    }
    if (io_reasoning_text) {
        *io_reasoning_text = reasoning_text;
    }

    agent_cleanup_inbound_msg(msg);

#if DAIMA_SKILL_SCOPED_TOOLS_ENABLED
    skill_tools_unregister_all();
#endif

    if (turn_err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "Agent turn failed: %s", daima_err_to_name(turn_err));
    }

#ifdef DAIMA_COMPACTION_RECOVERY_ENABLED
    if (turn_err == DAIMA_OK && msg && msg->chat_id[0]) {
        compaction_recovery_clear(msg->chat_id);
    }
#endif
#ifdef DAIMA_TODO_ENFORCER_ENABLED
    if (turn_err == DAIMA_OK && msg && msg->chat_id[0]) {
        int total_todos = 0;
        int completed_todos = 0;
        read_todo_counts(&total_todos, &completed_todos);
        todo_enforcer_record_progress(msg->chat_id, total_todos, completed_todos);
    }
#endif
#ifdef DAIMA_SESSION_RECOVERY_ENABLED
    if (turn_err == DAIMA_OK && msg && msg->chat_id[0]) {
        session_recovery_clear(msg->chat_id);
    }
#endif

    DAIMA_LOGI(TAG, "Free memory: %d bytes", (int)daima_get_free_memory());
}
