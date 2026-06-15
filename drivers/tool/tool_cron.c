/* cron 工具实现。 */

#include "drivers/tool/tool_cron.h"
#include "kernel/time/timer.h"
#include "runtime.h"
#include "drivers/channel/feishu/feishu_targets.h"
#include "bus.h"

#include <stdbool.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include "linux/printk.h"
#include "cjson.h"
static void format_epoch_local(int64_t epoch, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return;
    }

    if (epoch <= 0) {
        snprintf(buf, buf_size, "-");
        return;
    }

    time_t ts = (time_t)epoch;
    struct tm tm_value;
    if (!localtime_r(&ts, &tm_value)) {
        snprintf(buf, buf_size, "%lld", (long long)epoch);
        return;
    }

    if (strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S %Z", &tm_value) == 0) {
        snprintf(buf, buf_size, "%lld", (long long)epoch);
    }
}


static bool parse_hhmm(const char *text, uint32_t *out)
{
    if (!text || !out) return false;
    int hour = -1;
    int minute = -1;
    int second = 0;
    char tail = '\0';
    int n = sscanf(text, "%d:%d:%d%c", &hour, &minute, &second, &tail);
    if (n < 2 || n > 3) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    *out = (uint32_t)(hour * 3600 + minute * 60 + second);
    return true;
}

static bool parse_weekday_name(const char *text, uint8_t *bit)
{
    if (!text || !bit) return false;
    if (strcasecmp(text, "sun") == 0 || strcasecmp(text, "sunday") == 0 || strcasecmp(text, "0") == 0) { *bit = 1U << 0; return true; }
    if (strcasecmp(text, "mon") == 0 || strcasecmp(text, "monday") == 0 || strcasecmp(text, "1") == 0) { *bit = 1U << 1; return true; }
    if (strcasecmp(text, "tue") == 0 || strcasecmp(text, "tuesday") == 0 || strcasecmp(text, "2") == 0) { *bit = 1U << 2; return true; }
    if (strcasecmp(text, "wed") == 0 || strcasecmp(text, "wednesday") == 0 || strcasecmp(text, "3") == 0) { *bit = 1U << 3; return true; }
    if (strcasecmp(text, "thu") == 0 || strcasecmp(text, "thursday") == 0 || strcasecmp(text, "4") == 0) { *bit = 1U << 4; return true; }
    if (strcasecmp(text, "fri") == 0 || strcasecmp(text, "friday") == 0 || strcasecmp(text, "5") == 0) { *bit = 1U << 5; return true; }
    if (strcasecmp(text, "sat") == 0 || strcasecmp(text, "saturday") == 0 || strcasecmp(text, "6") == 0) { *bit = 1U << 6; return true; }
    return false;
}

static bool parse_weekdays(cJSON *item, uint8_t *out)
{
    if (!out) return false;
    if (!item) {
        *out = 0x7f;
        return true;
    }
    if (cJSON_IsNumber(item)) {
        int v = (int)item->valuedouble;
        if (v < 0 || v > 0x7f) return false;
        *out = (uint8_t)v;
        return true;
    }
    if (!cJSON_IsArray(item)) return false;

    uint8_t mask = 0;
    cJSON *child = NULL;
    cJSON_ArrayForEach(child, item) {
        uint8_t bit = 0;
        if (cJSON_IsNumber(child)) {
            int v = (int)child->valuedouble;
            if (v < 0 || v > 6) return false;
            bit = (uint8_t)(1U << v);
        } else if (cJSON_IsString(child)) {
            if (!parse_weekday_name(child->valuestring, &bit)) return false;
        } else {
            return false;
        }
        mask |= bit;
    }
    *out = mask;
    return true;
}

static void format_time_of_day(uint32_t seconds, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return;
    snprintf(buf, buf_size, "%02u:%02u:%02u", seconds / 3600U, (seconds % 3600U) / 60U, seconds % 60U);
}

static void format_weekdays(uint8_t weekdays, char *buf, size_t buf_size)
{
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (!buf || buf_size == 0) return;
    if ((weekdays & 0x7f) == 0x7f || weekdays == 0) {
        snprintf(buf, buf_size, "everyday");
        return;
    }
    size_t off = 0;
    for (int i = 0; i < 7; i++) {
        if ((weekdays & (1U << i)) == 0) continue;
        off += snprintf(buf + off, off < buf_size ? buf_size - off : 0, "%s%s", off ? "," : "", names[i]);
    }
    if (off == 0) snprintf(buf, buf_size, "-");
}

static const struct tool s_cron_tool = {
    .name = "cron",
    .description = "统一定时任务工具。action=add 创建周期、每日/每周或一次性任务；action=list 列出任务；action=remove 按 ID 删除任务。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"action\":{\"type\":\"string\",\"description\":\"add、list 或 remove\"},"
        "\"name\":{\"type\":\"string\",\"description\":\"任务名称\"},"
        "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' 固定间隔；'at' 指定 unix 时间戳；'daily'/'weekly' 按本地时钟触发\"},"
        "\"interval_s\":{\"type\":\"integer\",\"description\":\"间隔秒数（'every' 必填）\"},"
        "\"at_epoch\":{\"type\":\"integer\",\"description\":\"触发时间的 unix 时间戳（'at' 必填）\"},"
        "\"time\":{\"type\":\"string\",\"description\":\"本地时间 HH:MM 或 HH:MM:SS（'daily'/'weekly' 必填）\"},"
        "\"time_of_day_s\":{\"type\":\"integer\",\"description\":\"当天秒数（可替代 time）\"},"
        "\"weekdays\":{\"description\":\"星期列表或位图，0=Sun..6=Sat；工作日用 [1,2,3,4,5]\"},"
        "\"message\":{\"type\":\"string\",\"description\":\"任务触发时注入的消息\"},"
        "\"channel\":{\"type\":\"string\",\"description\":\"可选回复通道（'websocket' / 'feishu' / 'system'）。未填时优先使用当前通道\"},"
        "\"chat_id\":{\"type\":\"string\",\"description\":\"可选回复 chat_id。websocket 未填时使用当前会话；feishu 未填时优先使用配置或最近飞书会话\"},"
        "\"job_id\":{\"type\":\"string\",\"description\":\"remove 时要删除的 8 位任务 ID\"}"
        "},"
        "\"required\":[\"action\"]}",
    .execute = tool_cron_execute,
};

/* ── action=add ───────────────────────────────────────────────── */

static err_t cron_action_add_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return ERR_INVALID_ARG;
    }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
    const char *schedule_type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "schedule_type"));
    const char *message = cJSON_GetStringValue(cJSON_GetObjectItem(root, "message"));

    if (!name || !schedule_type || !message) {
        snprintf(output, output_size, "错误：缺少必要字段（name、schedule_type、message）");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    if (strlen(message) == 0) {
        snprintf(output, output_size, "错误：message 不能为空");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    cron_job_t job;
    memset(&job, 0, sizeof(job));
    strncpy(job.name, name, sizeof(job.name) - 1);
    strncpy(job.message, message, sizeof(job.message) - 1);

    /* 可选的 channel 与 chat_id */
    const char *channel = cJSON_GetStringValue(cJSON_GetObjectItem(root, "channel"));
    const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));
    if (channel) strncpy(job.channel, channel, sizeof(job.channel) - 1);
    if (chat_id) strncpy(job.chat_id, chat_id, sizeof(job.chat_id) - 1);

    if (strcmp(job.channel, CHAN_FEISHU) == 0 &&
        (job.chat_id[0] == '\0' || strcmp(job.chat_id, "cron") == 0)) {
        char default_chat_id[64];
        if (feishu_targets_get_default(default_chat_id, sizeof(default_chat_id))) {
            strncpy(job.chat_id, default_chat_id, sizeof(job.chat_id) - 1);
        }
    }

    if (strcmp(job.channel, CHAN_WEBSOCKET) == 0 &&
        (job.chat_id[0] == '\0' || strcmp(job.chat_id, "cron") == 0)) {
        snprintf(output, output_size,
                 "错误：cron action=add 使用 channel='websocket' 时必须提供有效 chat_id");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }
    if (strcmp(job.channel, CHAN_FEISHU) == 0 &&
        (job.chat_id[0] == '\0' || strcmp(job.chat_id, "cron") == 0)) {
        snprintf(output, output_size,
                 "错误：cron action=add 使用 channel='feishu' 时必须提供有效 chat_id，或在 config.json 的 feishu.default_chat_id 中配置默认接收人/群");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    if (strcmp(schedule_type, "every") == 0) {
        job.kind = CRON_KIND_EVERY;
        cJSON *interval = cJSON_GetObjectItem(root, "interval_s");
        if (!interval || !cJSON_IsNumber(interval) || interval->valuedouble <= 0) {
            snprintf(output, output_size, "错误：'every' 任务需要正数 interval_s");
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        job.interval_s = (uint32_t)interval->valuedouble;
        job.delete_after_run = false;
    } else if (strcmp(schedule_type, "at") == 0) {
        job.kind = CRON_KIND_AT;
        cJSON *at_epoch = cJSON_GetObjectItem(root, "at_epoch");
        if (!at_epoch || !cJSON_IsNumber(at_epoch)) {
            snprintf(output, output_size, "错误：'at' 任务需要 at_epoch（unix 时间戳）");
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        job.at_epoch = (int64_t)at_epoch->valuedouble;

        /* 检查是否已在过去 */
        time_t now = time(NULL);
        if (job.at_epoch <= now) {
            snprintf(output, output_size, "错误：at_epoch %lld 已在过去（当前=%lld）",
                     (long long)job.at_epoch, (long long)now);
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }

        /* 默认：一次性任务执行后删除 */
        cJSON *delete_j = cJSON_GetObjectItem(root, "delete_after_run");
        job.delete_after_run = delete_j ? cJSON_IsTrue(delete_j) : true;
    } else if (strcmp(schedule_type, "daily") == 0 || strcmp(schedule_type, "weekly") == 0) {
        job.kind = strcmp(schedule_type, "daily") == 0 ? CRON_KIND_DAILY : CRON_KIND_WEEKLY;
        cJSON *tod = cJSON_GetObjectItem(root, "time_of_day_s");
        cJSON *time_j = cJSON_GetObjectItem(root, "time");
        if (tod && cJSON_IsNumber(tod)) {
            job.time_of_day_s = (uint32_t)tod->valuedouble;
        } else if (time_j && cJSON_IsString(time_j)) {
            if (!parse_hhmm(time_j->valuestring, &job.time_of_day_s)) {
                snprintf(output, output_size, "错误：time 必须是 HH:MM 或 HH:MM:SS");
                cJSON_Delete(root);
                return ERR_INVALID_ARG;
            }
        } else {
            snprintf(output, output_size, "错误：'%s' 任务需要 time 或 time_of_day_s", schedule_type);
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        if (job.time_of_day_s >= 24U * 60U * 60U) {
            snprintf(output, output_size, "错误：time_of_day_s 必须小于 86400");
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        cJSON *weekdays = cJSON_GetObjectItem(root, "weekdays");
        if (job.kind == CRON_KIND_WEEKLY && !weekdays) {
            snprintf(output, output_size, "错误：'weekly' 任务需要 weekdays");
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        if (!parse_weekdays(weekdays, &job.weekdays)) {
            snprintf(output, output_size, "错误：weekdays 必须是 0..127 位图，或 [0..6]/['mon'..'sun'] 列表");
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        if (job.weekdays == 0) {
            snprintf(output, output_size, "错误：weekdays 不能为空");
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        job.delete_after_run = false;
    } else {
        snprintf(output, output_size, "错误：schedule_type 必须是 'every'、'at'、'daily' 或 'weekly'");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    cJSON_Delete(root);

    err_t err = cron_add_job(&job);
    if (err != 0) {
        snprintf(output, output_size, "错误：添加任务失败（%s）", err_name(err));
        return err;
    }

    /* 格式化成功响应 */
    if (job.kind == CRON_KIND_EVERY) {
        char next_run_text[64];
        format_epoch_local(job.next_run, next_run_text, sizeof(next_run_text));
        snprintf(output, output_size,
                 "OK：已添加周期任务 '%s'（id=%s），每 %lu 秒执行一次。下次执行时间=%s（epoch=%lld）。",
                 job.name, job.id, (unsigned long)job.interval_s,
                 next_run_text, (long long)job.next_run);
    } else if (job.kind == CRON_KIND_AT) {
        char at_text[64];
        format_epoch_local(job.at_epoch, at_text, sizeof(at_text));
        snprintf(output, output_size,
                 "OK：已添加一次性任务 '%s'（id=%s），触发时间=%s（epoch=%lld）。%s",
                 job.name, job.id, at_text, (long long)job.at_epoch,
                 job.delete_after_run ? "触发后将自动删除。" : "");
    } else {
        char next_run_text[64];
        char tod_text[16];
        char weekdays_text[64];
        format_epoch_local(job.next_run, next_run_text, sizeof(next_run_text));
        format_time_of_day(job.time_of_day_s, tod_text, sizeof(tod_text));
        format_weekdays(job.weekdays, weekdays_text, sizeof(weekdays_text));
        snprintf(output, output_size,
                 "OK：已添加%s任务 '%s'（id=%s），%s %s 触发。下次执行时间=%s（epoch=%lld）。",
                 job.kind == CRON_KIND_WEEKLY ? "每周" : "每日",
                 job.name, job.id, weekdays_text, tod_text,
                 next_run_text, (long long)job.next_run);
    }

    pr_info("cron add: %s", output);
    return 0;
}

/* ── action=list ───────────────────────────────────────────────── */

static err_t cron_action_list_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    const cron_job_t *jobs;
    int count;
    cron_list_jobs(&jobs, &count);

    if (count == 0) {
        snprintf(output, output_size, "没有已安排的定时任务。");
        return 0;
    }

    size_t off = 0;
    off += snprintf(output + off, output_size - off,
                    "已安排任务（%d）：\n", count);

    for (int i = 0; i < count && off < output_size - 1; i++) {
        const cron_job_t *j = &jobs[i];
        char next_run_text[64];
        char last_run_text[64];
        format_epoch_local(j->next_run, next_run_text, sizeof(next_run_text));
        format_epoch_local(j->last_run, last_run_text, sizeof(last_run_text));

        if (j->kind == CRON_KIND_EVERY) {
            off += snprintf(output + off, output_size - off,
                "  %d. [%s] \"%s\" — 每 %lu 秒，%s，next=%s（epoch=%lld），last=%s，ch=%s:%s\n",
                i + 1, j->id, j->name,
                (unsigned long)j->interval_s,
                j->enabled ? "启用" : "禁用",
                next_run_text, (long long)j->next_run, last_run_text,
                j->channel, j->chat_id);
        } else {
            char at_text[64];
            format_epoch_local(j->at_epoch, at_text, sizeof(at_text));
            off += snprintf(output + off, output_size - off,
                "  %d. [%s] \"%s\" — at=%s（epoch=%lld），next=%s，%s，last=%s，ch=%s:%s%s\n",
                i + 1, j->id, j->name,
                at_text, (long long)j->at_epoch, next_run_text,
                j->enabled ? "启用" : "禁用",
                last_run_text,
                j->channel, j->chat_id,
                j->delete_after_run ? "（自动删除）" : "");
        }
    }

    pr_info("cron list: %d jobs", count);
    return 0;
}

/* ── action=remove ─────────────────────────────────────────────── */

static err_t cron_action_remove_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return ERR_INVALID_ARG;
    }

    const char *job_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "job_id"));
    if (!job_id || strlen(job_id) == 0) {
        snprintf(output, output_size, "错误：缺少 'job_id' 字段");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    char job_id_copy[16] = {0};
    strncpy(job_id_copy, job_id, sizeof(job_id_copy) - 1);

    err_t err = cron_remove_job(job_id_copy);
    cJSON_Delete(root);

    if (err == 0) {
        snprintf(output, output_size, "OK：已移除定时任务 %s", job_id_copy);
    } else if (err == ERR_NOT_FOUND) {
        snprintf(output, output_size, "错误：未找到任务 '%s'", job_id_copy);
    } else {
        snprintf(output, output_size, "错误：移除任务失败（%s）", err_name(err));
    }

    pr_info("cron remove: %s -> %s", job_id_copy, err_name(err));
    return err;
}

err_t tool_cron_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root || !cJSON_IsObject(root)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    if (!action || !action[0]) {
        snprintf(output, output_size, "错误：缺少 action 字段（add/list/remove）");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    err_t err;
    if (strcmp(action, "add") == 0) {
        err = cron_action_add_execute(input_json, output, output_size);
    } else if (strcmp(action, "list") == 0) {
        err = cron_action_list_execute(input_json, output, output_size);
    } else if (strcmp(action, "remove") == 0) {
        err = cron_action_remove_execute(input_json, output, output_size);
    } else {
        snprintf(output, output_size, "错误：action 必须是 add、list 或 remove");
        err = ERR_INVALID_ARG;
    }

    cJSON_Delete(root);
    return err;
}

static int cron_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_cron_device = {
    .name = "cron",
    .description = "统一定时任务工具。action=add 创建周期、每日/每周或一次性任务；action=list 列出任务；action=remove 按 ID 删除任务。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"action\":{\"type\":\"string\",\"description\":\"add、list 或 remove\"},"
        "\"name\":{\"type\":\"string\",\"description\":\"任务名称\"},"
        "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' 固定间隔；'at' 指定 unix 时间戳；'daily'/'weekly' 按本地时钟触发\"},"
        "\"interval_s\":{\"type\":\"integer\",\"description\":\"间隔秒数（'every' 必填）\"},"
        "\"at_epoch\":{\"type\":\"integer\",\"description\":\"触发时间的 unix 时间戳（'at' 必填）\"},"
        "\"time\":{\"type\":\"string\",\"description\":\"本地时间 HH:MM 或 HH:MM:SS（'daily'/'weekly' 必填）\"},"
        "\"time_of_day_s\":{\"type\":\"integer\",\"description\":\"当天秒数（可替代 time）\"},"
        "\"weekdays\":{\"description\":\"星期列表或位图，0=Sun..6=Sat；工作日用 [1,2,3,4,5]\"},"
        "\"message\":{\"type\":\"string\",\"description\":\"任务触发时注入的消息\"},"
        "\"channel\":{\"type\":\"string\",\"description\":\"可选回复通道（'websocket' / 'feishu' / 'system'）。未填时优先使用当前通道\"},"
        "\"chat_id\":{\"type\":\"string\",\"description\":\"可选回复 chat_id。websocket 未填时使用当前会话；feishu 未填时优先使用配置或最近飞书会话\"},"
        "\"job_id\":{\"type\":\"string\",\"description\":\"remove 时要删除的 8 位任务 ID\"}"
        "},"
        "\"required\":[\"action\"]}",
};

static struct tool_driver s_cron_driver = {
    .name = "cron",
    .probe = cron_tool_probe,
    .execute = tool_cron_execute,
};

const struct tool_device *tool_cron_device(void)
{
    return &s_cron_device;
}

const struct tool_driver *tool_cron_driver(void)
{
    return &s_cron_driver;
}

const struct tool *tool_cron_definition(void)
{
    return &s_cron_tool;
}
