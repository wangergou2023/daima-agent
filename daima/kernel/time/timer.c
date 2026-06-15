/* 定时任务服务实现。 */

#include "kernel/time/timer.h"
#include "runtime.h"
#include "drivers/channel/feishu/feishu_targets.h"
#include "paths.h"
#include "autoconf.h"
#include "bus.h"
#include "os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "linux/printk.h"
#include "drivers/platform/platform.h"
#include "cJSON.h"
#include "linux/slab.h"
#define MAX_CRON_JOBS  CRON_MAX_JOBS

static cron_job_t s_jobs[MAX_CRON_JOBS];
static int s_job_count = 0;
static daima_task_t *s_cron_task = NULL;

static int cron_check_interval_ms(void)
{
    return runtime_config_get_cron_check_interval_ms();
}

static daima_err_t cron_save_jobs(void);

static const char *cron_kind_to_string(cron_kind_t kind)
{
    switch (kind) {
    case CRON_KIND_EVERY: return "every";
    case CRON_KIND_AT: return "at";
    case CRON_KIND_DAILY: return "daily";
    case CRON_KIND_WEEKLY: return "weekly";
    default: return "unknown";
    }
}

static uint8_t normalize_weekdays(uint8_t weekdays, cron_kind_t kind)
{
    if (kind == CRON_KIND_DAILY && weekdays == 0) {
        return 0x7f;
    }
    return weekdays & 0x7f;
}

static time_t next_local_schedule_after(time_t after, uint32_t time_of_day_s, uint8_t weekdays)
{
    if (time_of_day_s >= 24U * 60U * 60U) {
        return 0;
    }
    weekdays &= 0x7f;
    if (weekdays == 0) {
        weekdays = 0x7f;
    }

    for (int day_offset = 0; day_offset <= 7; day_offset++) {
        time_t probe = after + (time_t)day_offset * 86400;
        struct tm tm_value;
        if (!localtime_r(&probe, &tm_value)) {
            return 0;
        }
        if ((weekdays & (1U << tm_value.tm_wday)) == 0) {
            continue;
        }

        tm_value.tm_hour = (int)(time_of_day_s / 3600U);
        tm_value.tm_min = (int)((time_of_day_s % 3600U) / 60U);
        tm_value.tm_sec = (int)(time_of_day_s % 60U);
        time_t candidate = mktime(&tm_value);
        if (candidate > after) {
            return candidate;
        }
    }
    return 0;
}

static bool cron_sanitize_destination(cron_job_t *job)
{
    bool changed = false;
    if (!job) {
        return false;
    }

    if (job->channel[0] == '\0') {
        strncpy(job->channel, DAIMA_CHAN_SYSTEM, sizeof(job->channel) - 1);
        changed = true;
    }

    if (strcmp(job->channel, DAIMA_CHAN_FEISHU) == 0 &&
        (job->chat_id[0] == '\0' || strcmp(job->chat_id, "cron") == 0)) {
        char default_chat_id[64];
        if (feishu_targets_get_default(default_chat_id, sizeof(default_chat_id))) {
            strncpy(job->chat_id, default_chat_id, sizeof(job->chat_id) - 1);
            changed = true;
        }
    }

    if (strcmp(job->channel, DAIMA_CHAN_WEBSOCKET) == 0 ||
        strcmp(job->channel, DAIMA_CHAN_FEISHU) == 0) {
        if (job->chat_id[0] == '\0' || strcmp(job->chat_id, "cron") == 0) {
            pr_warn("Cron job %s has invalid chat_id, fallback to system:cron", job->id[0] ? job->id : "<new>");
            strncpy(job->channel, DAIMA_CHAN_SYSTEM, sizeof(job->channel) - 1);
            strncpy(job->chat_id, "cron", sizeof(job->chat_id) - 1);
            changed = true;
        }
    } else if (strcmp(job->channel, DAIMA_CHAN_SYSTEM) == 0) {
        if (job->chat_id[0] == '\0') {
            strncpy(job->chat_id, "cron", sizeof(job->chat_id) - 1);
            changed = true;
        }
    } else {
        pr_warn("Cron job %s has unknown channel '%s', fallback to system:cron", job->id[0] ? job->id : "<new>", job->channel);
        strncpy(job->channel, DAIMA_CHAN_SYSTEM, sizeof(job->channel) - 1);
        strncpy(job->chat_id, "cron", sizeof(job->chat_id) - 1);
        changed = true;
    }

    return changed;
}

/* ── 持久化 ──────────────────────────────────────────────── */

static void cron_generate_id(char *id_buf)
{
    uint32_t r = daima_random();
    snprintf(id_buf, 9, "%08x", (unsigned int)r);
}

static daima_err_t cron_load_jobs(void)
{
    FILE *f = fopen(daima_path_cron_file(), "r");
    if (!f) {
        pr_info("No cron file found, starting fresh");
        s_job_count = 0;
        return DAIMA_OK;
    }

    /* 读取整个文件 */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 8192) {
        pr_warn("Cron file invalid size: %ld", fsize);
        fclose(f);
        s_job_count = 0;
        return DAIMA_OK;
    }

    char *buf = kmalloc(fsize + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return DAIMA_ERR_NO_MEM;
    }

    size_t n = fread(buf, 1, fsize, f);
    buf[n] = '\0';
    fclose(f);

    /* 解析 JSON */
    cJSON *root = cJSON_Parse(buf);
    kfree(buf);

    if (!root) {
        pr_warn("Failed to parse cron JSON");
        s_job_count = 0;
        return DAIMA_OK;
    }

    cJSON *jobs_arr = cJSON_GetObjectItem(root, "jobs");
    if (!jobs_arr || !cJSON_IsArray(jobs_arr)) {
        cJSON_Delete(root);
        s_job_count = 0;
        return DAIMA_OK;
    }

    s_job_count = 0;
    bool repaired = false;
    cJSON *item;
    cJSON_ArrayForEach(item, jobs_arr) {
        if (s_job_count >= MAX_CRON_JOBS) break;

        cron_job_t *job = &s_jobs[s_job_count];
        memset(job, 0, sizeof(cron_job_t));

        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "id"));
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
        const char *kind_str = cJSON_GetStringValue(cJSON_GetObjectItem(item, "kind"));
        const char *message = cJSON_GetStringValue(cJSON_GetObjectItem(item, "message"));
        const char *channel = cJSON_GetStringValue(cJSON_GetObjectItem(item, "channel"));
        const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "chat_id"));

        if (!id || !name || !kind_str || !message) continue;

        strncpy(job->id, id, sizeof(job->id) - 1);
        strncpy(job->name, name, sizeof(job->name) - 1);
        strncpy(job->message, message, sizeof(job->message) - 1);
        strncpy(job->channel, channel ? channel : DAIMA_CHAN_SYSTEM,
                sizeof(job->channel) - 1);
        strncpy(job->chat_id, chat_id ? chat_id : "cron",
                sizeof(job->chat_id) - 1);
        if (cron_sanitize_destination(job)) {
            repaired = true;
        }

        cJSON *enabled_j = cJSON_GetObjectItem(item, "enabled");
        job->enabled = enabled_j ? cJSON_IsTrue(enabled_j) : true;

        cJSON *delete_j = cJSON_GetObjectItem(item, "delete_after_run");
        job->delete_after_run = delete_j ? cJSON_IsTrue(delete_j) : false;

        if (strcmp(kind_str, "every") == 0) {
            job->kind = CRON_KIND_EVERY;
            cJSON *interval = cJSON_GetObjectItem(item, "interval_s");
            job->interval_s = (interval && cJSON_IsNumber(interval))
                              ? (uint32_t)interval->valuedouble : 0;
        } else if (strcmp(kind_str, "at") == 0) {
            job->kind = CRON_KIND_AT;
            cJSON *at_epoch = cJSON_GetObjectItem(item, "at_epoch");
            job->at_epoch = (at_epoch && cJSON_IsNumber(at_epoch))
                            ? (int64_t)at_epoch->valuedouble : 0;
        } else if (strcmp(kind_str, "daily") == 0 || strcmp(kind_str, "weekly") == 0) {
            job->kind = strcmp(kind_str, "daily") == 0 ? CRON_KIND_DAILY : CRON_KIND_WEEKLY;
            cJSON *tod = cJSON_GetObjectItem(item, "time_of_day_s");
            cJSON *weekdays = cJSON_GetObjectItem(item, "weekdays");
            job->time_of_day_s = (tod && cJSON_IsNumber(tod)) ? (uint32_t)tod->valuedouble : 0;
            job->weekdays = (weekdays && cJSON_IsNumber(weekdays)) ? (uint8_t)weekdays->valuedouble : 0;
            job->weekdays = normalize_weekdays(job->weekdays, job->kind);
        } else {
            continue; /* 未知类型，跳过 */
        }

        cJSON *last_run = cJSON_GetObjectItem(item, "last_run");
        job->last_run = (last_run && cJSON_IsNumber(last_run))
                        ? (int64_t)last_run->valuedouble : 0;

        cJSON *next_run = cJSON_GetObjectItem(item, "next_run");
        job->next_run = (next_run && cJSON_IsNumber(next_run))
                        ? (int64_t)next_run->valuedouble : 0;

        s_job_count++;
    }

    cJSON_Delete(root);
    if (repaired) {
        cron_save_jobs();
    }
    pr_info("Loaded %d cron jobs", s_job_count);
    return DAIMA_OK;
}

static daima_err_t cron_save_jobs(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *jobs_arr = cJSON_CreateArray();

    for (int i = 0; i < s_job_count; i++) {
        cron_job_t *job = &s_jobs[i];
        cJSON *item = cJSON_CreateObject();

        cJSON_AddStringToObject(item, "id", job->id);
        cJSON_AddStringToObject(item, "name", job->name);
        cJSON_AddBoolToObject(item, "enabled", job->enabled);
        cJSON_AddStringToObject(item, "kind", cron_kind_to_string(job->kind));

        if (job->kind == CRON_KIND_EVERY) {
            cJSON_AddNumberToObject(item, "interval_s", job->interval_s);
        } else if (job->kind == CRON_KIND_AT) {
            cJSON_AddNumberToObject(item, "at_epoch", (double)job->at_epoch);
        } else if (job->kind == CRON_KIND_DAILY || job->kind == CRON_KIND_WEEKLY) {
            cJSON_AddNumberToObject(item, "time_of_day_s", job->time_of_day_s);
            cJSON_AddNumberToObject(item, "weekdays", job->weekdays);
        }

        cJSON_AddStringToObject(item, "message", job->message);
        cJSON_AddStringToObject(item, "channel", job->channel);
        cJSON_AddStringToObject(item, "chat_id", job->chat_id);
        cJSON_AddNumberToObject(item, "last_run", (double)job->last_run);
        cJSON_AddNumberToObject(item, "next_run", (double)job->next_run);
        cJSON_AddBoolToObject(item, "delete_after_run", job->delete_after_run);

        cJSON_AddItemToArray(jobs_arr, item);
    }

    cJSON_AddItemToObject(root, "jobs", jobs_arr);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        pr_err("Failed to serialize cron jobs");
        return DAIMA_ERR_NO_MEM;
    }

    FILE *f = fopen(daima_path_cron_file(), "w");
    if (!f) {
        pr_err("Failed to open %s for writing", daima_path_cron_file());
        kfree(json_str);
        return DAIMA_FAIL;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, f);
    fclose(f);
    kfree(json_str);

    if (written != len) {
        pr_err("Cron save incomplete: %d/%d bytes", (int)written, (int)len);
        return DAIMA_FAIL;
    }

    pr_info("Saved %d cron jobs to %s", s_job_count, daima_path_cron_file());
    return DAIMA_OK;
}

/* ── 到期任务处理 ───────────────────────────────────────── */

static void cron_process_due_jobs(void)
{
    time_t now = time(NULL);

    bool changed = false;

    for (int i = 0; i < s_job_count; i++) {
        cron_job_t *job = &s_jobs[i];
        if (!job->enabled) continue;
        if (job->next_run <= 0) continue;
        if (job->next_run > now) continue;

        /* 任务到期 — 执行 */
        pr_info("Cron job firing: %s (%s)", job->name, job->id);

        /* 将消息推入入站队列 */
        struct message msg;
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.channel, job->channel, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, job->chat_id, sizeof(msg.chat_id) - 1);
        strncpy(msg.source, DAIMA_MSG_SOURCE_CRON, sizeof(msg.source) - 1);
        msg.content = strdup(job->message);

        if (msg.content) {
            daima_err_t err = message_bus_push_inbound(&msg);
            if (err != DAIMA_OK) {
                pr_warn("Failed to push cron message: %s", daima_err_to_name(err));
                kfree(msg.content);
            }
        }

        /* 更新状态 */
        job->last_run = now;

        if (job->kind == CRON_KIND_AT) {
            /* 一次性任务：禁用或删除 */
            if (job->delete_after_run) {
                /* 通过移动数组删除 */
                pr_info("Deleting one-shot job: %s", job->name);
                for (int j = i; j < s_job_count - 1; j++) {
                    s_jobs[j] = s_jobs[j + 1];
                }
                s_job_count--;
                i--; /* 重新检查该索引 */
            } else {
                job->enabled = false;
                job->next_run = 0;
            }
        } else if (job->kind == CRON_KIND_EVERY) {
            /* 固定间隔任务：从本次触发时间继续向后滚动，避免短间隔追赶风暴。 */
            job->next_run = now + job->interval_s;
        } else if (job->kind == CRON_KIND_DAILY || job->kind == CRON_KIND_WEEKLY) {
            job->next_run = next_local_schedule_after(now, job->time_of_day_s, job->weekdays);
        }

        changed = true;
    }

    if (changed) {
        cron_save_jobs();
    }
}

static void cron_task_main(void *arg)
{
    (void)arg;

    while (1) {
        daima_task_delay(cron_check_interval_ms());
        cron_process_due_jobs();
    }
}

/* ── 为新任务计算初始 next_run ───────────────────── */

static void compute_initial_next_run(cron_job_t *job)
{
    time_t now = time(NULL);

    if (job->kind == CRON_KIND_EVERY) {
        job->next_run = now + job->interval_s;
    } else if (job->kind == CRON_KIND_AT) {
        if (job->at_epoch > now) {
            job->next_run = job->at_epoch;
        } else {
            /* 已在过去 */
            job->next_run = 0;
            job->enabled = false;
        }
    } else if (job->kind == CRON_KIND_DAILY || job->kind == CRON_KIND_WEEKLY) {
        job->weekdays = normalize_weekdays(job->weekdays, job->kind);
        job->next_run = next_local_schedule_after(now, job->time_of_day_s, job->weekdays);
        if (job->next_run <= 0) {
            job->enabled = false;
        }
    }
}

/* ── 对外接口 ───────────────────────────────────────────────── */

daima_err_t cron_service_init(void)
{
    return cron_load_jobs();
}

daima_err_t cron_service_start(void)
{
    if (s_cron_task) {
        pr_warn("Cron task already running");
        return DAIMA_OK;
    }

    /* 为所有启用且未设置 next_run 的任务重新计算 */
    time_t now = time(NULL);
    for (int i = 0; i < s_job_count; i++) {
        cron_job_t *job = &s_jobs[i];
        if (job->enabled && job->next_run <= 0) {
            if (job->kind == CRON_KIND_EVERY) {
                job->next_run = now + job->interval_s;
            } else if (job->kind == CRON_KIND_AT && job->at_epoch > now) {
                job->next_run = job->at_epoch;
            } else if (job->kind == CRON_KIND_DAILY || job->kind == CRON_KIND_WEEKLY) {
                job->weekdays = normalize_weekdays(job->weekdays, job->kind);
                job->next_run = next_local_schedule_after(now, job->time_of_day_s, job->weekdays);
            }
        }
    }

    bool ok = daima_task_create(
        cron_task_main,
        "cron",
        4096,
        NULL,
        4,
        &s_cron_task
    );
    if (!ok || !s_cron_task) {
        pr_err("Failed to create cron task");
        return DAIMA_FAIL;
    }

    pr_info("Cron service started (%d jobs, check every %ds)", s_job_count, cron_check_interval_ms() / 1000);
    return DAIMA_OK;
}

void cron_service_stop(void)
{
    if (s_cron_task) {
        daima_task_delete(s_cron_task);
        s_cron_task = NULL;
        pr_info("Cron service stopped");
    }
}

daima_err_t cron_add_job(cron_job_t *job)
{
    if (s_job_count >= MAX_CRON_JOBS) {
        pr_warn("Max cron jobs reached (%d)", MAX_CRON_JOBS);
        return DAIMA_ERR_NO_MEM;
    }

    /* 生成 ID */
    cron_generate_id(job->id);

    /* 存储前校验/规范化 channel 和 chat_id。 */
    cron_sanitize_destination(job);

    /* 计算初始 next_run */
    job->enabled = true;
    job->last_run = 0;
    compute_initial_next_run(job);

    /* 拷贝到静态数组 */
    s_jobs[s_job_count] = *job;
    s_job_count++;

    cron_save_jobs();

    pr_info("Added cron job: %s (%s) kind=%s next_run=%lld", job->name, job->id, cron_kind_to_string(job->kind), (long long)job->next_run);
    return DAIMA_OK;
}

daima_err_t cron_remove_job(const char *job_id)
{
    for (int i = 0; i < s_job_count; i++) {
        if (strcmp(s_jobs[i].id, job_id) == 0) {
            pr_info("Removing cron job: %s (%s)", s_jobs[i].name, job_id);

            /* 将剩余任务前移 */
            for (int j = i; j < s_job_count - 1; j++) {
                s_jobs[j] = s_jobs[j + 1];
            }
            s_job_count--;

            cron_save_jobs();
            return DAIMA_OK;
        }
    }

    pr_warn("Cron job not found: %s", job_id);
    return DAIMA_ERR_NOT_FOUND;
}

void cron_list_jobs(const cron_job_t **jobs, int *count)
{
    *jobs = s_jobs;
    *count = s_job_count;
}
