/* 会话历史上下文压缩：将较老的中间消息汇总为结构化摘要。 */

#include "context_compress.h"
#include "context_ops.h"
#include "compaction.h"
#include "turn_common.h"
#include "runtime.h"

#include "autoconf.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "cJSON.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "compress";
#define COMPRESS_QUEUE_MAX       16

enum {
    DEFAULT_COMPRESS_TRIGGER_MSGS = 12,
    DEFAULT_COMPRESS_PROTECT_LAST = 6,
};

typedef struct {
    char chat_id[32];
    bool queued;
    bool running;
    bool rerun;
} compress_job_t;

static pthread_t s_worker_thread;
static pthread_mutex_t s_worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_worker_cond = PTHREAD_COND_INITIALIZER;
static compress_job_t s_jobs[COMPRESS_QUEUE_MAX];
static bool s_worker_started = false;

static context_compress_cfg_t load_cfg(void)
{
    context_compress_cfg_t cfg = {
        .enabled = DAIMA_CONTEXT_COMPRESS_ENABLED != 0,
        .trigger_msgs = DEFAULT_COMPRESS_TRIGGER_MSGS,
        .max_chars = DAIMA_CONTEXT_COMPRESS_MAX_CHARS,
        .protect_first = DAIMA_CONTEXT_COMPRESS_PROTECT_FIRST,
        .protect_last = DEFAULT_COMPRESS_PROTECT_LAST,
        .max_passes = DAIMA_CONTEXT_COMPRESS_MAX_PASSES,
    };

    cfg.enabled = agent_env_bool_or_default("DAIMA_COMPRESS_ENABLED", cfg.enabled);
    cfg.trigger_msgs = runtime_config_get_compress_trigger_msgs();
    cfg.max_chars = agent_env_int_or_default("DAIMA_COMPRESS_MAX_CHARS", cfg.max_chars);
    cfg.protect_first = agent_env_int_or_default("DAIMA_COMPRESS_PROTECT_FIRST", cfg.protect_first);
    cfg.protect_last = runtime_config_get_compress_keep_msgs();
    cfg.max_passes = agent_env_int_or_default("DAIMA_COMPRESS_MAX_PASSES", cfg.max_passes);

    if (cfg.trigger_msgs < 4) cfg.trigger_msgs = 4;
    if (cfg.max_chars < 4000) cfg.max_chars = 4000;
    if (cfg.protect_first < 1) cfg.protect_first = 1;
    if (cfg.protect_last < 2) cfg.protect_last = 2;
    if (cfg.max_passes < 1) cfg.max_passes = 1;
    return cfg;
}

static compress_job_t *find_job_slot_locked(const char *chat_id, bool create_if_missing)
{
    compress_job_t *free_slot = NULL;
    for (int i = 0; i < COMPRESS_QUEUE_MAX; i++) {
        if (s_jobs[i].chat_id[0] && strcmp(s_jobs[i].chat_id, chat_id) == 0) {
            return &s_jobs[i];
        }
        if (!free_slot && s_jobs[i].chat_id[0] == '\0') {
            free_slot = &s_jobs[i];
        }
    }
    if (!create_if_missing || !free_slot) {
        return NULL;
    }
    snprintf(free_slot->chat_id, sizeof(free_slot->chat_id), "%s", chat_id);
    free_slot->queued = false;
    free_slot->running = false;
    free_slot->rerun = false;
    return free_slot;
}

static void *compression_worker_loop(void *arg)
{
    (void)arg;

    while (1) {
        char chat_id[32] = {0};

        pthread_mutex_lock(&s_worker_mutex);
        while (1) {
            compress_job_t *picked = NULL;
            for (int i = 0; i < COMPRESS_QUEUE_MAX; i++) {
                if (s_jobs[i].queued) {
                    picked = &s_jobs[i];
                    break;
                }
            }
            if (picked) {
                picked->queued = false;
                picked->running = true;
                snprintf(chat_id, sizeof(chat_id), "%s", picked->chat_id);
                break;
            }
            pthread_cond_wait(&s_worker_cond, &s_worker_mutex);
        }
        pthread_mutex_unlock(&s_worker_mutex);

        context_compress_cfg_t cfg = load_cfg();
        context_compress_session_in_background(chat_id, &cfg);

        pthread_mutex_lock(&s_worker_mutex);
        compress_job_t *job = find_job_slot_locked(chat_id, false);
        if (job) {
            if (job->rerun) {
                job->rerun = false;
                job->queued = true;
            } else {
                job->chat_id[0] = '\0';
                job->queued = false;
            }
            job->running = false;
        }
        pthread_cond_signal(&s_worker_cond);
        pthread_mutex_unlock(&s_worker_mutex);
    }

    return NULL;
}

daima_err_t context_compressor_maybe_compact(
    const char *chat_id,
    const char *system_prompt,
    cJSON **messages_io,
    bool *did_compact)
{
    if (did_compact) {
        *did_compact = false;
    }
    if (!chat_id || !messages_io || !*messages_io || !cJSON_IsArray(*messages_io)) {
        return DAIMA_ERR_INVALID_ARG;
    }

    context_compress_cfg_t cfg = load_cfg();
    if (!cfg.enabled) {
        return DAIMA_OK;
    }

    for (int pass = 0; pass < cfg.max_passes; pass++) {
        size_t approx_chars = context_compress_estimate_chars(system_prompt, *messages_io);
        if (!context_compress_needed(*messages_io, &cfg, approx_chars)) {
            break;
        }

        int n = context_compress_message_count(*messages_io);

        DAIMA_LOGI(
            TAG,
            "Session %s needs compression: pass=%d msgs=%d approx_chars=%u",
            chat_id,
            pass + 1,
            n,
            (unsigned)approx_chars);

        if (IS_ENABLED(CONFIG_DAIMA_COMPACTION_RECOVERY_ENABLED)) {
            compaction_recovery_snapshot(chat_id);
        }

        daima_err_t err = context_compress_compact_once(chat_id, messages_io, &cfg);
        if (err != DAIMA_OK) {
            return err;
        }
        if (did_compact) {
            *did_compact = true;
        }
    }

    return DAIMA_OK;
}

daima_err_t context_compressor_init(void)
{
    pthread_mutex_lock(&s_worker_mutex);
    if (s_worker_started) {
        pthread_mutex_unlock(&s_worker_mutex);
        return DAIMA_OK;
    }
    if (pthread_create(&s_worker_thread, NULL, compression_worker_loop, NULL) != 0) {
        pthread_mutex_unlock(&s_worker_mutex);
        DAIMA_LOGE(TAG, "Failed to start background compression worker");
        return DAIMA_FAIL;
    }
    pthread_detach(s_worker_thread);
    s_worker_started = true;
    pthread_mutex_unlock(&s_worker_mutex);
    DAIMA_LOGI(TAG, "Background compression worker started");
    return DAIMA_OK;
}

daima_err_t context_compressor_schedule(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&s_worker_mutex);
    compress_job_t *job = find_job_slot_locked(chat_id, true);
    if (!job) {
        pthread_mutex_unlock(&s_worker_mutex);
        DAIMA_LOGW(TAG, "Compression queue full, skip schedule for %s", chat_id);
        return DAIMA_FAIL;
    }

    if (job->running) {
        job->rerun = true;
    } else {
        job->queued = true;
    }
    pthread_cond_signal(&s_worker_cond);
    pthread_mutex_unlock(&s_worker_mutex);
    return DAIMA_OK;
}

daima_err_t context_compressor_schedule_if_needed(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    context_compress_cfg_t cfg = load_cfg();
    if (!cfg.enabled) {
        return DAIMA_OK;
    }

    cJSON *messages = context_compress_load_session_messages(chat_id);
    if (!messages) {
        return DAIMA_FAIL;
    }

    size_t approx_chars = context_compress_estimate_chars("", messages);
    int n = context_compress_message_count(messages);
    bool needed = context_compress_needed(messages, &cfg, approx_chars);
    cJSON_Delete(messages);

    if (!needed) {
        DAIMA_LOGD(
            TAG,
            "Skip background compression schedule for %s: msgs=%d approx_chars=%u below threshold",
            chat_id,
            n,
            (unsigned)approx_chars);
        return DAIMA_OK;
    }

    return context_compressor_schedule(chat_id);
}
