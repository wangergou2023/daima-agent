/* Transcript 结构化记录存储实现。 */
#include "transcript.h"

#include "paths.h"
#include "cjson.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "drivers/skill/skill_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char s_transcript_dir[256];

/* ──── 初始化 ──── */

err_t transcript_init(void)
{
    snprintf(s_transcript_dir, sizeof(s_transcript_dir),
             "%s/data/transcripts", path_memory_dir());

    /* mkdir -p */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", s_transcript_dir);
    int ret = system(cmd);
    (void)ret;

    pr_info("Transcript store at %s", s_transcript_dir);
    return 0;
}

/* ──── 内部辅助 ──── */

static void chat_id_to_slug(const char *chat_id, char *buf, size_t size)
{
    if (!chat_id || !buf || size == 0)
        return;
    size_t i;
    for (i = 0; i < size - 1 && chat_id[i]; i++) {
        char ch = chat_id[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
            buf[i] = ch;
        } else {
            buf[i] = '_';
        }
    }
    buf[i] = '\0';
}

static int count_tokens_in_string(const char *str)
{
    if (!str || !str[0])
        return 0;
    int count = 0;
    char *buf = kmalloc(strlen(str) + 1, GFP_KERNEL);
    if (!buf)
        return 0;
    strcpy(buf, str);
    char *token = strtok(buf, " ");
    while (token) {
        count++;
        token = strtok(NULL, " ");
    }
    kfree(buf);
    return count;
}

static bool skill_tag_overlaps(const char *filter_tags, const char *record_tags)
{
    if (!filter_tags || !filter_tags[0])
        return true; /* no filter → match all */
    if (!record_tags || !record_tags[0])
        return false;

    char *buf = kmalloc(strlen(filter_tags) + 1, GFP_KERNEL);
    if (!buf)
        return false;
    strcpy(buf, filter_tags);
    char *token = strtok(buf, " ");
    bool found = false;
    while (token) {
        if (strcasestr(record_tags, token)) {
            found = true;
            break;
        }
        token = strtok(NULL, " ");
    }
    kfree(buf);
    return found;
}

/* 从 JSON 文件解析为 transcript_record_t */
static err_t parse_transcript_json(const char *filepath, transcript_record_t *rec)
{
    FILE *f = fopen(filepath, "r");
    if (!f)
        return ERR_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 32768) {
        fclose(f);
        return ERR_INVALID_SIZE;
    }

    char *buf = kmalloc(sz + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return ERR_NO_MEM;
    }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    kfree(buf);
    if (!root)
        return ERR_FAIL;

    memset(rec, 0, sizeof(*rec));

#define GET_STR(field, key) \
    do { const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(root, key)); \
         if (v) strscpy(rec->field, v, sizeof(rec->field)); } while (0)

#define GET_NUM(field, key) \
    do { cJSON *item = cJSON_GetObjectItem(root, key); \
         if (item && cJSON_IsNumber(item)) rec->field = (int)cJSON_GetNumberValue(item); } while (0)

    GET_STR(record_id, "record_id");
    GET_STR(task_id, "task_id");
    GET_STR(chat_id, "chat_id");
    GET_STR(user_input, "user_input");
    GET_STR(skill_tags, "skill_tags");
    GET_STR(tools_used, "tools_used");
    GET_STR(execution_summary, "execution_summary");
    GET_STR(output, "output");
    GET_STR(result_status, "result_status");
    GET_STR(error_code, "error_code");
    GET_STR(error_msg, "error_msg");
    GET_STR(executed_by, "executed_by");
    GET_STR(agent_id, "agent_id");
    GET_STR(routing_decision, "routing_decision");
    GET_STR(match_agent_id, "match_agent_id");
    GET_STR(fallback_reason, "fallback_reason");
    GET_STR(artifacts, "artifacts");

    GET_NUM(total_tokens, "total_tokens");
    GET_NUM(model_calls, "model_calls");
    GET_NUM(tool_calls, "tool_calls");
    GET_NUM(duration_ms, "duration_ms");

    {
        cJSON *item = cJSON_GetObjectItem(root, "match_score");
        if (item && cJSON_IsNumber(item))
            rec->match_score = (float)cJSON_GetNumberValue(item);
    }
    {
        cJSON *item = cJSON_GetObjectItem(root, "timestamp");
        if (item && cJSON_IsNumber(item))
            rec->timestamp = (time_t)cJSON_GetNumberValue(item);
    }

#undef GET_STR
#undef GET_NUM

    cJSON_Delete(root);
    return 0;
}

/* ──── 写入 ──── */

err_t transcript_append(const transcript_record_t *record)
{
    if (!record || !record->record_id[0] || !record->chat_id[0])
        return ERR_INVALID_ARG;

    /* 按 chat_id slug 分目录 */
    char slug[64];
    chat_id_to_slug(record->chat_id, slug, sizeof(slug));

    char chat_dir[512];
    snprintf(chat_dir, sizeof(chat_dir), "%s/%s", s_transcript_dir, slug);
    mkdir(chat_dir, 0755);

    char filepath[640];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", chat_dir, record->record_id);

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return ERR_NO_MEM;

    cJSON_AddStringToObject(root, "record_id", record->record_id);
    cJSON_AddStringToObject(root, "task_id", record->task_id);
    cJSON_AddStringToObject(root, "chat_id", record->chat_id);
    cJSON_AddStringToObject(root, "user_input", record->user_input);
    cJSON_AddStringToObject(root, "skill_tags", record->skill_tags);
    cJSON_AddStringToObject(root, "tools_used", record->tools_used);
    cJSON_AddStringToObject(root, "execution_summary", record->execution_summary);
    cJSON_AddStringToObject(root, "output", record->output);
    cJSON_AddStringToObject(root, "result_status", record->result_status);
    cJSON_AddStringToObject(root, "error_code", record->error_code);
    cJSON_AddStringToObject(root, "error_msg", record->error_msg);
    cJSON_AddStringToObject(root, "executed_by", record->executed_by);
    cJSON_AddStringToObject(root, "agent_id", record->agent_id);
    cJSON_AddStringToObject(root, "routing_decision", record->routing_decision);
    cJSON_AddStringToObject(root, "match_agent_id", record->match_agent_id);
    cJSON_AddStringToObject(root, "fallback_reason", record->fallback_reason);
    cJSON_AddStringToObject(root, "artifacts", record->artifacts);
    cJSON_AddNumberToObject(root, "total_tokens", record->total_tokens);
    cJSON_AddNumberToObject(root, "model_calls", record->model_calls);
    cJSON_AddNumberToObject(root, "tool_calls", record->tool_calls);
    cJSON_AddNumberToObject(root, "duration_ms", record->duration_ms);
    cJSON_AddNumberToObject(root, "match_score", (double)record->match_score);
    cJSON_AddNumberToObject(root, "timestamp", (double)record->timestamp);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json)
        return ERR_NO_MEM;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        kfree(json);
        return ERR_FAIL;
    }
    fprintf(f, "%s\n", json);
    fclose(f);
    kfree(json);

    pr_info("Transcript saved: %s", filepath);
    return 0;
}

/* ──── 查询 ──── */

err_t transcript_query(const char *filter_skill_tags,
                       const char *filter_status,
                       time_t since_ts,
                       int limit,
                       transcript_record_t *out_records,
                       int out_capacity,
                       int *out_count)
{
    *out_count = 0;
    if (out_capacity <= 0 || !out_records)
        return ERR_INVALID_ARG;
    if (limit <= 0)
        limit = TRANSCRIPT_QUERY_MAX;

    DIR *root = opendir(s_transcript_dir);
    if (!root)
        return 0;  /* no transcripts yet */

    struct dirent *chat_entry;
    while ((chat_entry = readdir(root)) != NULL && *out_count < limit) {
        if (chat_entry->d_name[0] == '.')
            continue;

        /* 每个 chat 子目录 */
        char chat_dir[512];
        snprintf(chat_dir, sizeof(chat_dir), "%s/%s",
                 s_transcript_dir, chat_entry->d_name);

        DIR *chat = opendir(chat_dir);
        if (!chat)
            continue;

        struct dirent *txn_entry;
        while ((txn_entry = readdir(chat)) != NULL && *out_count < limit) {
            const char *name = txn_entry->d_name;
            if (name[0] == '.')
                continue;

            /* 只处理 .json 文件 */
            const char *ext = strrchr(name, '.');
            if (!ext || strcmp(ext, ".json") != 0)
                continue;

            char filepath[640];
            snprintf(filepath, sizeof(filepath), "%s/%s", chat_dir, name);

            transcript_record_t rec;
            if (parse_transcript_json(filepath, &rec) != 0)
                continue;

            /* 过滤：状态 */
            if (filter_status && filter_status[0] &&
                strcmp(rec.result_status, filter_status) != 0)
                continue;

            /* 过滤：时间 */
            if (since_ts > 0 && rec.timestamp < since_ts)
                continue;

            /* 过滤：技能标签 */
            if (!skill_tag_overlaps(filter_skill_tags, rec.skill_tags))
                continue;

            out_records[*out_count] = rec;
            (*out_count)++;
        }
        closedir(chat);
    }
    closedir(root);
    return 0;
}

/* ──── 标签提取 ──── */

/* 技术关键词 → 标准化标签映射表。
 * 每个关键词映射到一个标签，扫描用户输入和输出文本进行匹配。
 * 标签用空格分隔，写入 Transcript 供 HR 聚类使用。 */
typedef struct {
    const char *keyword;       /* 匹配关键词（小写，大小写不敏感匹配） */
    const char *tag;           /* 标准化标签 */
} keyword_tag_entry_t;

static const keyword_tag_entry_t s_keyword_tags[] = {
    /* 前端 */
    {"react", "react"},
    {"vue", "vue"},
    {"angular", "angular"},
    {"javascript", "javascript"},
    {"typescript", "typescript"},
    {"html", "html"},
    {"css", "css"},
    {"前端", "frontend"},
    {"组件", "component"},
    {"表单", "form"},
    {"登录", "authentication"},
    {"ui", "ui"},
    {"页面", "web-page"},
    {"tailwind", "tailwind"},
    {"next.js", "nextjs"},
    {"nextjs", "nextjs"},

    /* 后端 */
    {"python", "python"},
    {"golang", "go"},
    {"rust", "rust"},
    {"java", "java"},
    {"spring", "spring"},
    {"django", "django"},
    {"flask", "flask"},
    {"fastapi", "fastapi"},
    {"api", "api"},
    {"rest", "api"},
    {"graphql", "graphql"},
    {"微服务", "microservices"},
    {"数据库", "database"},
    {"mysql", "mysql"},
    {"postgresql", "postgresql"},
    {"mongodb", "mongodb"},
    {"redis", "redis"},
    {"sql", "sql"},

    /* 嵌入式/底层 */
    {"c语言", "c"},
    {"c11", "c"},
    {"c++", "cpp"},
    {"嵌入式", "embedded"},
    {"stm32", "stm32"},
    {"mips", "mips"},
    {"arm", "arm"},
    {"linux", "linux"},
    {"驱动", "driver"},
    {"gpio", "gpio"},
    {"i2c", "i2c"},
    {"spi", "spi"},
    {"freertos", "freertos"},
    {"固件", "firmware"},
    {"内核", "kernel"},

    /* 通用开发 */
    {"代码审查", "code-review"},
    {"review", "code-review"},
    {"审查", "code-review"},
    {"重构", "refactoring"},
    {"refactor", "refactoring"},
    {"调试", "debugging"},
    {"debug", "debugging"},
    {"测试", "testing"},
    {"单元测试", "testing"},
    {"test", "testing"},
    {"bug", "bug-fix"},
    {"修复", "bug-fix"},
    {"fix", "bug-fix"},
    {"编译", "build"},
    {"构建", "build"},
    {"build", "build"},
    {"部署", "deployment"},
    {"deploy", "deployment"},
    {"docker", "docker"},
    {"容器", "container"},
    {"ci/cd", "ci-cd"},
    {"配置", "configuration"},
    {"config", "configuration"},

    /* 数据分析/AI */
    {"数据分析", "data-analysis"},
    {"ai", "ai"},
    {"llm", "llm"},
    {"机器学习", "machine-learning"},
    {"深度学习", "deep-learning"},
    {"训练", "training"},
    {"模型", "model"},
    {"推理", "inference"},
    {"agent", "agent"},
    {"prompt", "prompt-engineering"},

    /* 文档 */
    {"文档", "documentation"},
    {"readme", "documentation"},
    {"markdown", "documentation"},
    {"注释", "documentation"},
    {"写一个", "code-generation"},
    {"实现", "code-generation"},
    {"implement", "code-generation"},
    {"创建", "code-generation"},
    {"生成", "code-generation"},
    {"帮我写", "code-generation"},
    {"帮我", "code-generation"},

    {NULL, NULL}  /* sentinel */
};

err_t transcript_extract_skill_tags(const char *user_input,
                                    const char *output_text,
                                    char *out_tags,
                                    size_t buf_size)
{
    if (!user_input || !out_tags || buf_size == 0)
        return ERR_INVALID_ARG;

    out_tags[0] = '\0';

    /* 构建扫描文本：用户输入 + 输出文本（用于更丰富的匹配） */
    char scan_buf[TRANSCRIPT_USER_INPUT_LEN * 2];
    size_t scan_len = 0;
    size_t input_len = strlen(user_input);
    if (input_len >= sizeof(scan_buf))
        input_len = sizeof(scan_buf) - 1;
    memcpy(scan_buf, user_input, input_len);
    scan_len = input_len;

    if (output_text && output_text[0]) {
        if (scan_len > 0 && scan_len < sizeof(scan_buf) - 1)
            scan_buf[scan_len++] = ' ';
        size_t out_len = strlen(output_text);
        size_t remaining = sizeof(scan_buf) - scan_len - 1;
        if (out_len > remaining)
            out_len = remaining;
        memcpy(scan_buf + scan_len, output_text, out_len);
        scan_len += out_len;
    }
    scan_buf[scan_len] = '\0';

    /* 转为小写用于大小写不敏感匹配 */
    char scan_lower[TRANSCRIPT_USER_INPUT_LEN * 2];
    size_t i;
    for (i = 0; i < scan_len && i < sizeof(scan_lower) - 1; i++) {
        char ch = scan_buf[i];
        scan_lower[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
    }
    scan_lower[i] = '\0';

    /* 扫描关键词表 */
    size_t off = 0;
    const keyword_tag_entry_t *entry;
    for (entry = s_keyword_tags; entry->keyword != NULL; entry++) {
        if (strstr(scan_lower, entry->keyword)) {
            /* 检查标签是否已存在（去重） */
            if (off == 0 || !strstr(out_tags, entry->tag)) {
                if (off > 0 && off < buf_size - 1)
                    out_tags[off++] = ' ';
                size_t remaining = buf_size - off - 1;
                size_t tag_len = strlen(entry->tag);
                if (tag_len > remaining)
                    tag_len = remaining;
                memcpy(out_tags + off, entry->tag, tag_len);
                off += tag_len;
                out_tags[off] = '\0';
            }
        }
    }

    return 0;
}
