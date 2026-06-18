/* Turn 收尾清理：分发最终回复、保存会话、释放资源、清除恢复标记。
 * 负责处理取消/错误/正常三种结局，统一调用 cleanup/clear 钩子。 */

#include "turn_finish.h"

#include <stdio.h>
#include <stdlib.h>

#include "turn_common.h"
#include "turn_persist.h"
#include "compaction.h"
#include "recovery.h"
#include "todo.h"
#include "paths.h"
#include "cjson.h"
#include "autoconf.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "os.h"
#include "drivers/platform/platform.h"
#if SKILL_SCOPED_TOOLS_ENABLED
#include "drivers/skill/skill_tools.h"
#include "linux/slab.h"
#endif
#include "ralph.h"
#include "turn_dispatch.h"
#ifdef TODO_ENFORCER_ENABLED
/** 读取 TODO 文件并统计 total/completed 数量。 */
static void read_todo_counts(int *out_total, int *out_completed)
{
    *out_total = 0;
    *out_completed = 0;

    FILE *f = fopen(path_todo_file(), "r");
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

    char *buf = kzalloc((size_t)size + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    kfree(buf);
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

/** Turn 收尾入口。
 *  取消 → 释放资源并返回；正常/错误 → 分发回复、保存会话、清理恢复标记和 TODO 进度。
 *  @param io_final_text      最终回复文本（可能被修改或释放）
 *  @param io_reasoning_text  推理文本（可能被释放）
 *  @param turn_err           turn_run 返回的错误码
 *  @param iteration          实际 LLM 调用轮次
 *  @param tool_budget_exhausted 是否预算耗尽
 *  @param cancelled          是否被取消 */
void agent_turn_finish(
    struct message *msg,
    char **io_final_text,
    char **io_reasoning_text,
    err_t turn_err,
    int iteration,
    bool tool_budget_exhausted,
    bool cancelled)
{
    char *final_text = io_final_text ? *io_final_text : NULL;
    char *reasoning_text = io_reasoning_text ? *io_reasoning_text : NULL;

    if (cancelled) {
        kfree(final_text);
        kfree(reasoning_text);
        final_text = NULL;
        reasoning_text = NULL;
        if (io_final_text) {
            *io_final_text = NULL;
        }
        if (io_reasoning_text) {
            *io_reasoning_text = NULL;
        }
        pr_info("Skip final response for cancelled turn %s:%s", msg ? msg->channel : "-", msg ? msg->chat_id : "-");
#if SKILL_SCOPED_TOOLS_ENABLED
        skill_tools_unregister_all();
#endif
        agent_cleanup_inbound_msg(msg);
        return;
    }

    if (final_text && final_text[0]) {
        ralph_loop_append_warning_if_needed(msg->chat_id, iteration, &final_text);
        agent_turn_save_session(msg, final_text, reasoning_text, iteration);
        agent_turn_queue_outbound_text(msg, final_text, reasoning_text, true);
        final_text = NULL;
        kfree(reasoning_text);
        reasoning_text = NULL;
    } else {
        kfree(final_text);
        kfree(reasoning_text);
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

#if SKILL_SCOPED_TOOLS_ENABLED
    skill_tools_unregister_all();
#endif

    if (turn_err != 0) {
        pr_err("Agent turn failed: %s", err_name(turn_err));
    }

    if (IS_ENABLED(CONFIG_COMPACTION_RECOVERY_ENABLED) &&
        turn_err == 0 && msg && msg->chat_id[0]) {
        compaction_recovery_clear(msg->chat_id);
    }
    if (IS_ENABLED(CONFIG_TODO_ENFORCER_ENABLED) &&
        turn_err == 0 && msg && msg->chat_id[0]) {
        int total_todos = 0;
        int completed_todos = 0;
        read_todo_counts(&total_todos, &completed_todos);
        todo_enforcer_record_progress(msg->chat_id, total_todos, completed_todos);
    }
    if (IS_ENABLED(CONFIG_SESSION_RECOVERY_ENABLED) &&
        turn_err == 0 && msg && msg->chat_id[0]) {
        session_recovery_clear(msg->chat_id);
    }

    if (turn_err == 0 && msg && msg->chat_id[0]) {
        dispatch_compress_context(msg->chat_id);
    }

    pr_info("Free memory: %d bytes", (int)platform_free_memory());
}
