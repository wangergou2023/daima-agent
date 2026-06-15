#include "turn_prepare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "channel_policy.h"
#include "debug.h"
#include "turn_common.h"
#include "compaction.h"
#include "context_build.h"
#include "rules.h"
#include "recovery.h"
#include "todo.h"
#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/session_store.h"
#include "autoconf.h"
#include "env.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#ifdef ENABLE_VISION
#include "drivers/vision/vision_capture.h"
#include "linux/slab.h"
#endif
static char *build_current_turn_content(const struct message *msg)
{
    const char *source = agent_msg_source_or_default(msg);
    const char *content = (msg && msg->content) ? msg->content : "";

    if (!agent_msg_is_synthetic_event(msg)) {
        return strdup(content);
    }

    if (strcmp(source, DAIMA_MSG_SOURCE_CRON) == 0) {
        const char *fmt =
            "这是系统注入的定时提醒事件，不是用户刚刚发送的新消息。\n"
            "事件来源：cron\n"
            "处理要求：若提醒已到点，请直接自然地向用户发出提醒；"
            "不要把这段内容当成用户回复，也不要否认之前已经成功设置的提醒。\n\n"
            "提醒内容：%s";
        size_t need = snprintf(NULL, 0, fmt, content) + 1;
        char *buf = kzalloc(need, GFP_KERNEL);
        if (!buf) {
            return NULL;
        }
        snprintf(buf, need, fmt, content);
        return buf;
    }

    if (strcmp(source, DAIMA_MSG_SOURCE_HEARTBEAT) == 0) {
        const char *fmt =
            "这是系统触发的后台巡检事件，不是用户刚刚发送的新消息。\n"
            "事件来源：heartbeat\n"
            "请把下面内容当作系统任务说明执行；若无需用户感知，就不要假装这是用户在说话。\n\n"
            "任务内容：%s";
        size_t need = snprintf(NULL, 0, fmt, content) + 1;
        char *buf = kzalloc(need, GFP_KERNEL);
        if (!buf) {
            return NULL;
        }
        snprintf(buf, need, fmt, content);
        return buf;
    }

    if (strcmp(source, DAIMA_MSG_SOURCE_INTERNAL) == 0) {
        return strdup(
            "这是内部控制事件，不是用户消息。\n"
            "不要把它当成对话内容，也不要向用户复述任何内部载荷。"
        );
    }

    return strdup(content);
}

#ifdef ENABLE_VISION
static cJSON *build_user_vision_content(const char *text, const char *image_path)
{
    char local_path[256] = {0};
    bool cleanup_local_path = false;

    if (image_path && image_path[0]) {
        strscpy(local_path, image_path, sizeof(local_path));
    } else {
#ifdef BUILD_FOR_MIPS
        err_t cap_err = vision_capture_jpeg(NULL, local_path, sizeof(local_path));
        if (cap_err != 0) {
            return NULL;
        }
        cleanup_local_path = true;
#else
        (void)text;
        return NULL;
#endif
    }

    llm_image_content_t img = {0};
    err_t read_err = llm_image_read_file(local_path, &img);
    if (read_err != 0) {
        pr_warn("Failed to read image for multimodal request: %s (%s)", local_path, err_name(read_err));
        if (cleanup_local_path) {
            unlink(local_path);
        }
        return NULL;
    }

    cJSON *content = llm_create_multimodal_content(text, &img, 1);
    llm_image_content_free(&img);
    pr_info("Attached image to multimodal request: %s", local_path);

    if (cleanup_local_path) {
        const char *keep = daima_env_get("DAIMA_VISION_KEEP_SNAPSHOT");
        if (!keep || !keep[0]) {
            unlink(local_path);
        }
    }

    return content;
}
#endif

static void append_turn_context_prompt(char *prompt, size_t size, const struct message *msg)
{
    if (!prompt || size == 0 || !msg) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    const char *source = agent_msg_source_or_default(msg);
    const char *kind = agent_msg_is_synthetic_event(msg) ? "系统触发事件" : "用户新消息";
    int n = snprintf(
        prompt + off, size - off,
        "\n## 当前轮运行时上下文\n\n"
        "### 当前消息\n"
        "- 来源通道: %s\n"
        "- 来源 chat_id: %s\n"
        "- 消息来源类型: %s\n"
        "- 当前消息性质: %s\n"
        "- 若本轮使用 cron action=add 发回当前会话，请设置 channel 与 chat_id 为来源值。\n",
        msg->channel[0] ? msg->channel : "(unknown)",
        msg->chat_id[0] ? msg->chat_id : "(empty)",
        source,
        kind);
    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
    }
}

#ifdef RULES_INJECTION_ENABLED
static void prepend_rules_prompt(char *prompt, size_t size, const char *rules)
{
    if (!prompt || size == 0 || !rules || !rules[0]) {
        return;
    }

    char existing[CONTEXT_BUF_SIZE];
    strscpy(existing, prompt, sizeof(existing));
    int n = snprintf(prompt, size, "%s\n%s", rules, existing);
    if (n < 0 || (size_t)n >= size) {
        prompt[size - 1] = '\0';
    }
}
#endif

static void append_session_facts_prompt(char *prompt, size_t size, const char *chat_id)
{
    if (!prompt || size == 0 || !chat_id || !chat_id[0]) {
        return;
    }

    char facts_buf[2048];
    if (session_store_read_facts(chat_id, facts_buf, sizeof(facts_buf)) != 0 || !facts_buf[0]) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    bool has_session_reference = strstr(prompt, "\n## 会话参考\n") != NULL;
    int n = snprintf(
        prompt + off, size - off,
        "%s### 稳定事实卡片\n"
        "以下内容是从更早轮次中提炼出的稳定偏好、约束、已确认决定。\n"
        "把它们当作长期有效的上下文；若与用户当前这轮明确新指令冲突，以当前新指令为准。\n\n"
        "%s\n",
        has_session_reference ? "\n" : "\n## 会话参考\n\n",
        facts_buf);

    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
    }
}

static void append_session_summary_prompt(char *prompt, size_t size, const char *chat_id)
{
    if (!prompt || size == 0 || !chat_id || !chat_id[0]) {
        return;
    }

    char summary_buf[BUF_XLARGE];
    if (session_store_read_summary(chat_id, summary_buf, sizeof(summary_buf)) != 0 || !summary_buf[0]) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    bool has_session_reference = strstr(prompt, "\n## 会话参考\n") != NULL;
    int n = snprintf(
        prompt + off, size - off,
        "%s### 最近一次上下文压缩摘要\n"
        "以下内容是对更早对话的结构化交接总结，用来帮助延续上下文。\n"
        "它不是新的用户输入；如果与当前这轮的明确要求冲突，以当前这轮为准。\n\n"
        "%s\n",
        has_session_reference ? "\n" : "\n## 会话参考\n\n",
        summary_buf);

    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
    }
}

err_t agent_turn_prepare(
    const struct message *msg,
    const struct plan *plan,
    char *system_prompt,
    size_t system_prompt_size,
    char *history_json,
    size_t history_json_size,
    cJSON **out_messages)
{
    if (!msg || !system_prompt || system_prompt_size == 0 || !history_json || history_json_size == 0 || !out_messages) {
        return ERR_INVALID_ARG;
    }

    *out_messages = NULL;

    char prompt_prefix[BUF_XLARGE] = {0};
    if (system_prompt[0]) {
        strscpy(prompt_prefix, system_prompt, sizeof(prompt_prefix));
    }

    context_build_system_prompt_for_channel(msg->channel, system_prompt, system_prompt_size);
    if (IS_ENABLED(CONFIG_DAIMA_RULES_INJECTION_ENABLED)) {
        char rules_buf[8192];
        if (rules_injection_load(rules_buf, sizeof(rules_buf)) == 0 && rules_buf[0]) {
            prepend_rules_prompt(system_prompt, system_prompt_size, rules_buf);
        }
    }
    append_session_summary_prompt(system_prompt, system_prompt_size, msg->chat_id);
    if (IS_ENABLED(CONFIG_DAIMA_COMPACTION_RECOVERY_ENABLED)) {
        compaction_recovery_inject(msg->chat_id, system_prompt, system_prompt_size);
    }
    if (IS_ENABLED(CONFIG_DAIMA_TODO_ENFORCER_ENABLED)) {
        todo_enforcer_inject_prompt(msg->chat_id, system_prompt, system_prompt_size);
    }
    if (IS_ENABLED(CONFIG_DAIMA_SESSION_RECOVERY_ENABLED)) {
        session_recovery_t rec = session_recovery_check(msg->chat_id);
        if (rec.has_crash) {
            session_recovery_inject_prompt(msg->chat_id, system_prompt, system_prompt_size);
            session_recovery_clear(msg->chat_id);
        }
    }
    append_session_facts_prompt(system_prompt, system_prompt_size, msg->chat_id);
    append_turn_context_prompt(system_prompt, system_prompt_size, msg);
    agent_channel_policy_append(system_prompt, system_prompt_size, msg);
    if (prompt_prefix[0]) {
        size_t off = strnlen(system_prompt, system_prompt_size - 1);
        if (off < system_prompt_size - 1) {
            int n = strscpy(system_prompt + off, prompt_prefix, system_prompt_size - off);
            if (n < 0 || (size_t)n >= system_prompt_size - off) {
                system_prompt[system_prompt_size - 1] = '\0';
            }
        }
    }
    (void)plan;
    context_fix_truncated_utf8(system_prompt, strnlen(system_prompt, system_prompt_size));

    agent_prompt_dump_snapshot(msg, system_prompt);
    pr_info("LLM turn context: channel=%s chat_id=%s source=%s", msg->channel, msg->chat_id, agent_msg_source_or_default(msg));

    session_store_get_history_json(msg->chat_id, history_json, history_json_size, AGENT_MAX_HISTORY);

    cJSON *messages = cJSON_Parse(history_json);
    if (!messages) messages = cJSON_CreateArray();
    if (!messages) {
        return ERR_NO_MEM;
    }

    cJSON *turn_msg = cJSON_CreateObject();
    if (!turn_msg) {
        cJSON_Delete(messages);
        return ERR_NO_MEM;
    }
    const char *role = agent_msg_role_for_current_turn(msg);
    char *current_content = build_current_turn_content(msg);
    if (!current_content) {
        cJSON_Delete(turn_msg);
        cJSON_Delete(messages);
        return ERR_NO_MEM;
    }

    cJSON_AddStringToObject(turn_msg, "role", role);
#ifdef ENABLE_VISION
    if (strcmp(role, "user") == 0) {
        cJSON *vision_content = build_user_vision_content(msg->content, msg->image_path);
        if (vision_content) {
            cJSON_AddItemToObject(turn_msg, "content", vision_content);
        } else if (msg->image_path && msg->image_path[0]) {
            cJSON_AddStringToObject(
                turn_msg,
                "content",
                "用户发送了一张图片，但当前这次请求没有成功附带图片内容。不要臆测图片细节；请明确说明当前无法读取这张图片，并提示用户稍后重试。");
        } else {
            cJSON_AddStringToObject(turn_msg, "content", current_content);
        }
    } else {
        cJSON_AddStringToObject(turn_msg, "content", current_content);
    }
#else
    cJSON_AddStringToObject(turn_msg, "content", current_content);
#endif
    kfree(current_content);
    cJSON_AddItemToArray(messages, turn_msg);

    *out_messages = messages;
    return 0;
}
