/* 后台自进化复盘：
 * - 从最近会话提炼长期记忆
 * - 生成技能更新建议，落入待审阅队列
 */

#include "learning.h"

#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/memory_store.h"
#include "drivers/memory/session_store.h"
#include "paths.h"
#include "autoconf.h"
#include "drivers/skill/skill_loader.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "log.h"

static const char *TAG = "learn";

#define REVIEW_QUEUE_MAX         16
#define REVIEW_MSGS_MAX          24
#define REVIEW_HISTORY_BUF_SIZE  (96 * 1024)
#define REVIEW_PROMPT_BUF_SIZE   (64 * 1024)
#define REVIEW_MEMORY_BUF_SIZE   (16 * 1024)

typedef struct {
    char chat_id[32];
    bool queued;
    bool running;
    bool rerun;
} review_job_t;

static pthread_t s_review_thread;
static pthread_mutex_t s_review_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_review_cond = PTHREAD_COND_INITIALIZER;
static review_job_t s_review_jobs[REVIEW_QUEUE_MAX];
static bool s_review_started = false;

static review_job_t *find_review_job_locked(const char *chat_id, bool create_if_missing)
{
    review_job_t *free_slot = NULL;
    for (int i = 0; i < REVIEW_QUEUE_MAX; i++) {
        if (s_review_jobs[i].chat_id[0] && strcmp(s_review_jobs[i].chat_id, chat_id) == 0) {
            return &s_review_jobs[i];
        }
        if (!free_slot && s_review_jobs[i].chat_id[0] == '\0') {
            free_slot = &s_review_jobs[i];
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

static char *trim_ascii(char *s)
{
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    size_t len = strlen(s);
    while (len > 0) {
        char ch = s[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
        s[--len] = '\0';
    }
    return s;
}

static char *extract_json_object(char *text)
{
    if (!text) return NULL;
    char *start = strchr(text, '{');
    char *end = strrchr(text, '}');
    if (!start || !end || end <= start) return NULL;
    end[1] = '\0';
    return start;
}

static const char *history_label_for_role(const char *role)
{
    if (!role || !role[0]) return "USER";
    if (strcmp(role, "assistant") == 0) return "ASSISTANT";
    if (strcmp(role, "system") == 0) return "SYSTEM";
    return "USER";
}

static void append_history_text(cJSON *messages, char *buf, size_t size)
{
    size_t off = 0;
    const cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItem((cJSON *)msg, "role");
        cJSON *content = cJSON_GetObjectItem((cJSON *)msg, "content");
        if (!role || !cJSON_IsString(role) || !content || !cJSON_IsString(content)) {
            continue;
        }
        const char *label = history_label_for_role(role->valuestring);
        int n = snprintf(buf + off, size - off, "[%s]\n%s\n\n", label, content->valuestring);
        if (n < 0 || (size_t)n >= size - off) {
            buf[size - 1] = '\0';
            return;
        }
        off += (size_t)n;
    }
}

static bool memory_line_exists(const char *haystack, const char *needle)
{
    return haystack && needle && strstr(haystack, needle) != NULL;
}

static daima_err_t merge_long_term_memory(cJSON *items)
{
    if (!items || !cJSON_IsArray(items) || cJSON_GetArraySize(items) <= 0) {
        return DAIMA_OK;
    }

    char current[REVIEW_MEMORY_BUF_SIZE];
    current[0] = '\0';
    memory_read_long_term(current, sizeof(current));

    char next[REVIEW_MEMORY_BUF_SIZE];
    snprintf(next, sizeof(next), "%s", current);

    size_t off = strlen(next);
    if (off == 0) {
        off += snprintf(next + off, sizeof(next) - off, "# Long-term Memory\n\n## 自进化记忆\n");
    } else if (!strstr(next, "## 自进化记忆")) {
        off += snprintf(next + off, sizeof(next) - off, "%s## 自进化记忆\n",
                        off > 0 && next[off - 1] != '\n' ? "\n\n" : "\n");
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (!cJSON_IsString(item) || !item->valuestring) continue;
        char temp[256];
        snprintf(temp, sizeof(temp), "%s", item->valuestring);
        char *line = trim_ascii(temp);
        if (!line[0]) continue;
        if (memory_line_exists(next, line)) continue;
        int n = snprintf(next + off, sizeof(next) - off, "- %s\n", line);
        if (n < 0 || (size_t)n >= sizeof(next) - off) break;
        off += (size_t)n;
    }

    if (strcmp(next, current) == 0) {
        return DAIMA_OK;
    }
    return memory_write_long_term(next);
}

static daima_err_t append_skill_review_queue(cJSON *skill_obj, const char *chat_id)
{
    if (!skill_obj || !cJSON_IsObject(skill_obj)) {
        return DAIMA_OK;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(skill_obj, "action"));
    if (!action || strcmp(action, "none") == 0) {
        return DAIMA_OK;
    }

    FILE *f = fopen(daima_path_skill_review_queue_file(), "a");
    if (!f) {
        DAIMA_LOGE(TAG, "Cannot open %s", daima_path_skill_review_queue_file());
        return DAIMA_FAIL;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    const char *target_skill = cJSON_GetStringValue(cJSON_GetObjectItem(skill_obj, "target_skill"));
    const char *reason = cJSON_GetStringValue(cJSON_GetObjectItem(skill_obj, "reason"));

    fprintf(f, "\n## %s | chat=%s\n", ts, chat_id ? chat_id : "");
    fprintf(f, "- action: %s\n", action);
    fprintf(f, "- target_skill: %s\n", target_skill && target_skill[0] ? target_skill : "(new skill)");
    if (reason && reason[0]) {
        fprintf(f, "- reason: %s\n", reason);
    }

    cJSON *changes = cJSON_GetObjectItem(skill_obj, "proposed_changes");
    if (changes && cJSON_IsArray(changes) && cJSON_GetArraySize(changes) > 0) {
        fprintf(f, "- proposed_changes:\n");
        const cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, changes) {
            if (cJSON_IsString(entry) && entry->valuestring) {
                fprintf(f, "  - %s\n", entry->valuestring);
            }
        }
    }
    fclose(f);
    DAIMA_LOGI(TAG, "Queued skill review for chat %s", chat_id);
    return DAIMA_OK;
}

static void run_review_for_chat(const char *chat_id)
{
    char *history_json = calloc(1, REVIEW_HISTORY_BUF_SIZE);
    char *history_text = calloc(1, REVIEW_PROMPT_BUF_SIZE);
    char *memory_text = calloc(1, REVIEW_MEMORY_BUF_SIZE);
    char *prompt = calloc(1, REVIEW_PROMPT_BUF_SIZE);
    char skills_summary[4096];
    if (!history_json || !history_text || !memory_text || !prompt) {
        goto cleanup;
    }

    if (session_store_get_history_json(chat_id, history_json, REVIEW_HISTORY_BUF_SIZE, REVIEW_MSGS_MAX) != DAIMA_OK) {
        goto cleanup;
    }

    cJSON *messages = cJSON_Parse(history_json);
    if (!messages || !cJSON_IsArray(messages) || cJSON_GetArraySize(messages) <= 1) {
        cJSON_Delete(messages);
        goto cleanup;
    }

    append_history_text(messages, history_text, REVIEW_PROMPT_BUF_SIZE);
    memory_read_long_term(memory_text, REVIEW_MEMORY_BUF_SIZE);
    skills_summary[0] = '\0';
    skill_loader_build_summary(skills_summary, sizeof(skills_summary));

    snprintf(
        prompt,
        REVIEW_PROMPT_BUF_SIZE,
        "你是代马（Daima）的后台学习复盘助手。请根据最近会话，提炼两类内容：\n"
        "1. 值得写入长期记忆的稳定事实（用户偏好、环境信息、长期约束）\n"
        "2. 值得更新/创建技能的可复用经验\n\n"
        "规则：\n"
        "- 只保留长期有效、可复用的信息\n"
        "- 不要保存临时寒暄或一次性细节\n"
        "- 优先建议更新已有 skill，而不是新建\n"
        "- 你的输出必须是 JSON，对象结构固定如下：\n"
        "{\n"
        "  \"long_term_memory\": [\"...\"],\n"
        "  \"skill_suggestion\": {\n"
        "    \"action\": \"none|update|create\",\n"
        "    \"target_skill\": \"skill-name or empty\",\n"
        "    \"reason\": \"一句话原因\",\n"
        "    \"proposed_changes\": [\"...\", \"...\"]\n"
        "  }\n"
        "}\n\n"
        "现有长期记忆：\n%s\n\n"
        "现有技能摘要：\n%s\n\n"
        "最近会话：\n%s",
        memory_text[0] ? memory_text : "(empty)",
        skills_summary[0] ? skills_summary : "(empty)",
        history_text
    );

    cJSON *req = cJSON_CreateArray();
    cJSON *user = cJSON_CreateObject();
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    if (!req || !user) {
        cJSON_Delete(req);
        cJSON_Delete(user);
        cJSON_Delete(messages);
        goto cleanup;
    }
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", prompt);
    cJSON_AddItemToArray(req, user);

    daima_err_t err = llm_chat_tools(
        "你是一个严格输出 JSON 的后台学习复盘助手，不要调用工具，不要输出解释文字。",
        req,
        NULL,
        &resp);
    cJSON_Delete(req);
    cJSON_Delete(messages);
    if (err != DAIMA_OK || resp.tool_use || !resp.text || !resp.text[0]) {
        llm_response_free(&resp);
        goto cleanup;
    }

    char *raw = resp.text;
    resp.text = NULL;
    resp.text_len = 0;
    llm_response_free(&resp);
    if (!raw) goto cleanup;
    char *json_text = extract_json_object(raw);
    if (!json_text) {
        free(raw);
        goto cleanup;
    }

    cJSON *review = cJSON_Parse(json_text);
    free(raw);
    if (!review || !cJSON_IsObject(review)) {
        cJSON_Delete(review);
        goto cleanup;
    }

    merge_long_term_memory(cJSON_GetObjectItem(review, "long_term_memory"));
    append_skill_review_queue(cJSON_GetObjectItem(review, "skill_suggestion"), chat_id);
    cJSON_Delete(review);

cleanup:
    free(history_json);
    free(history_text);
    free(memory_text);
    free(prompt);
}

static void *review_worker_loop(void *arg)
{
    (void)arg;
    while (1) {
        char chat_id[32] = {0};

        pthread_mutex_lock(&s_review_mutex);
        while (1) {
            review_job_t *picked = NULL;
            for (int i = 0; i < REVIEW_QUEUE_MAX; i++) {
                if (s_review_jobs[i].queued) {
                    picked = &s_review_jobs[i];
                    break;
                }
            }
            if (picked) {
                picked->queued = false;
                picked->running = true;
                snprintf(chat_id, sizeof(chat_id), "%s", picked->chat_id);
                break;
            }
            pthread_cond_wait(&s_review_cond, &s_review_mutex);
        }
        pthread_mutex_unlock(&s_review_mutex);

        run_review_for_chat(chat_id);

        pthread_mutex_lock(&s_review_mutex);
        review_job_t *job = find_review_job_locked(chat_id, false);
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
        pthread_cond_signal(&s_review_cond);
        pthread_mutex_unlock(&s_review_mutex);
    }
    return NULL;
}

daima_err_t learning_review_init(void)
{
    pthread_mutex_lock(&s_review_mutex);
    if (s_review_started) {
        pthread_mutex_unlock(&s_review_mutex);
        return DAIMA_OK;
    }
    if (pthread_create(&s_review_thread, NULL, review_worker_loop, NULL) != 0) {
        pthread_mutex_unlock(&s_review_mutex);
        DAIMA_LOGE(TAG, "Failed to start learning review worker");
        return DAIMA_FAIL;
    }
    pthread_detach(s_review_thread);
    s_review_started = true;
    pthread_mutex_unlock(&s_review_mutex);
    DAIMA_LOGI(TAG, "Learning review worker started");
    return DAIMA_OK;
}

daima_err_t learning_review_schedule(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&s_review_mutex);
    review_job_t *job = find_review_job_locked(chat_id, true);
    if (!job) {
        pthread_mutex_unlock(&s_review_mutex);
        DAIMA_LOGW(TAG, "Learning review queue full, skip chat %s", chat_id);
        return DAIMA_FAIL;
    }

    if (job->running) {
        job->rerun = true;
    } else {
        job->queued = true;
    }
    pthread_cond_signal(&s_review_cond);
    pthread_mutex_unlock(&s_review_mutex);
    return DAIMA_OK;
}
