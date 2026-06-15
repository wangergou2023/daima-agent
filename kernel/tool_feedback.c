#include "tool_feedback.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "tool_notify.h"
#include "cjson.h"
#include "linux/printk.h"
#include "text.h"
#include "linux/kernel.h"
static const char *tool_display_icon(const char *tool_name)
{
    if (!tool_name) return "⚙";
    if (strcmp(tool_name, "terminal") == 0) return "💻";
    if (strcmp(tool_name, "files") == 0) return "📖";
    if (strcmp(tool_name, "apply_patch") == 0) return "✏️";
    if (strcmp(tool_name, "weather") == 0) return "🌤️";
    if (strcmp(tool_name, "get_current_time") == 0) return "🕒";
    if (strcmp(tool_name, "cron") == 0) return "⏰";
    if (strcmp(tool_name, "skills") == 0) return "🧩";
    return "⚙";
}

static const char *tool_display_name(const char *tool_name)
{
    if (!tool_name) return "tool";
    return tool_name;
}

static const char *path_tail(const char *path)
{
    if (!path || !path[0]) return "";
    const char *slash = strrchr(path, '/');
    return (slash && slash[1]) ? slash + 1 : path;
}

static bool output_is_human_error(const char *tool_output)
{
    return tool_output &&
           (strncmp(tool_output, "错误：", strlen("错误：")) == 0 ||
            strncmp(tool_output, "Error:", strlen("Error:")) == 0);
}

static void summarize_tool_target(const char *tool_name, const char *tool_input, char *buf, size_t size)
{
    if (!buf || size == 0) return;
    buf[0] = '\0';

    cJSON *root = cJSON_Parse(tool_input ? tool_input : "{}");
    const char *value = NULL;

    if (root && cJSON_IsObject(root)) {
        if (tool_name && strcmp(tool_name, "terminal") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
            if (!value || !value[0]) {
                value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
            }
        } else if (strcmp(tool_name, "files") == 0 ||
                   strcmp(tool_name, "apply_patch") == 0) {
            value = path_tail(cJSON_GetStringValue(cJSON_GetObjectItem(root, "path")));
            if (!value || !value[0]) {
                value = path_tail(cJSON_GetStringValue(cJSON_GetObjectItem(root, "file_path")));
            }
            if (!value || !value[0]) {
                value = path_tail(cJSON_GetStringValue(cJSON_GetObjectItem(root, "filename")));
            }
        } else if (strcmp(tool_name, "weather") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "location"));
        } else if (strcmp(tool_name, "cron") == 0) {
            const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
            if (action && strcmp(action, "remove") == 0) {
                value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "job_id"));
            } else {
                value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
            }
        } else if (strcmp(tool_name, "session_search") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "query"));
        } else if (strcmp(tool_name, "skills") == 0) {
            value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
            if (!value || !value[0]) {
                value = cJSON_GetStringValue(cJSON_GetObjectItem(root, "pattern"));
            }
        }
    }

    if (value && value[0]) {
        text_shorten(value, buf, size, 44);
    }
    cJSON_Delete(root);
}

static bool tool_result_success(const char *tool_name, err_t exec_err, const char *tool_output, char *detail, size_t detail_size)
{
    if (detail && detail_size > 0) {
        detail[0] = '\0';
    }

    if (tool_name && strcmp(tool_name, "terminal") == 0) {
        cJSON *root = cJSON_Parse(tool_output ? tool_output : "{}");
        if (!root) {
            if (detail && detail_size > 0) snprintf(detail, detail_size, "执行失败");
            return false;
        }
        int exit_code = -1;
        cJSON *exit_j = cJSON_GetObjectItem(root, "exit_code");
        if (exit_j && cJSON_IsNumber(exit_j)) {
            exit_code = (int)exit_j->valuedouble;
        }
        cJSON *status_j = cJSON_GetObjectItem(root, "status");
        const char *status = cJSON_IsString(status_j) ? status_j->valuestring : NULL;
        cJSON *timed_j = cJSON_GetObjectItem(root, "timed_out");
        bool timed_out = cJSON_IsTrue(timed_j);
        bool ok = (exit_code == 0);

        if (detail && detail_size > 0) {
            if (timed_out) {
                snprintf(detail, detail_size, "超时");
            } else if (status && status[0] && strcmp(status, "ok") != 0) {
                strscpy(detail, status, detail_size);
            } else if (exit_code >= 0) {
                snprintf(detail, detail_size, "exit %d", exit_code);
            }
        }
        cJSON_Delete(root);
        return ok;
    }

    if (exec_err == 0) {
        return true;
    }

    if (detail && detail_size > 0) {
        if (output_is_human_error(tool_output)) {
            text_shorten(tool_output, detail, detail_size, detail_size > 1 ? detail_size - 1 : 0);
        } else {
            strscpy(detail, err_name(exec_err), detail_size);
        }
    }
    return false;
}

void agent_tool_feedback_send_activity(const struct message *msg,
                                       const char *tool_name,
                                       const char *tool_input,
                                       const char *tool_output,
                                       err_t exec_err,
                                       long elapsed_ms)
{
    if (!msg) {
        return;
    }

    char target[96];
    char detail[64];
    char line[256];
    summarize_tool_target(tool_name, tool_input, target, sizeof(target));

    bool ok = tool_result_success(tool_name, exec_err, tool_output, detail, sizeof(detail));
    if (target[0]) {
        if (ok) {
            snprintf(line, sizeof(line), "%s %s · %s · %.1fs",
                     tool_display_icon(tool_name),
                     tool_display_name(tool_name),
                     target,
                     (double)elapsed_ms / 1000.0);
        } else {
            snprintf(line, sizeof(line), "%s %s · %s · 失败%s%s",
                     tool_display_icon(tool_name),
                     tool_display_name(tool_name),
                     target,
                     detail[0] ? "：" : "",
                     detail);
        }
    } else {
        if (ok) {
            snprintf(line, sizeof(line), "%s %s · %.1fs",
                     tool_display_icon(tool_name),
                     tool_display_name(tool_name),
                     (double)elapsed_ms / 1000.0);
        } else {
            snprintf(line, sizeof(line), "%s %s · 失败%s%s",
                     tool_display_icon(tool_name),
                     tool_display_name(tool_name),
                     detail[0] ? "：" : "",
                     detail);
        }
    }

    tool_activity_event_t event = {
        .tool_name = tool_name,
        .tool_input = tool_input,
        .target = target,
        .detail = detail,
        .default_text = line,
        .ok = ok,
        .elapsed_ms = elapsed_ms,
    };
    err_t send_err = channel_runtime_send_tool_activity(msg, &event);
    if (send_err != 0) {
        pr_warn("Tool activity send failed for %s:%s: %s", msg->channel, msg->chat_id, err_name(send_err));
    }
}
