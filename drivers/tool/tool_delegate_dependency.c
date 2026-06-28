#include "drivers/tool/tool_delegate_dependency.h"

#include <ctype.h>
#include <string.h>

#include "cjson.h"
#include "delegate/delegate_task_store.h"
#include "delegate/delegate_session_json.h"
#include "drivers/tool/tool_delegate_protocol.h"
#include "linux/kernel.h"
#include "text.h"

#define DELEGATE_RESULT_JSON_MAX 3072

static bool delegate_task_store_snapshot_quiet(const char *task_id,
                                               delegate_task_record_t *out)
{
    return delegate_task_store_snapshot(task_id, out) == 0;
}

static void append_clipped_text(char *dst, size_t dst_size, const char *src, size_t clip_chars)
{
    char clipped[768];

    if (!dst || dst_size == 0 || !src || !src[0]) {
        return;
    }
    text_shorten(src, clipped, sizeof(clipped), (int)clip_chars);
    strlcat(dst, clipped, dst_size);
}

static void copy_trimmed_line(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }

    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n' || *src == '-') {
        src++;
    }
    len = strlen(src);
    while (len > 0) {
        char ch = src[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        len--;
    }
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void append_json_array_unique(cJSON *array, const char *value)
{
    cJSON *item = NULL;

    if (!array || !cJSON_IsArray(array) || !value || !value[0]) {
        return;
    }
    cJSON_ArrayForEach(item, array) {
        const char *existing = cJSON_GetStringValue(item);
        if (existing && strcmp(existing, value) == 0) {
            return;
        }
    }
    cJSON_AddItemToArray(array, cJSON_CreateString(value));
}

static void append_dependency_default_evidence(cJSON *evidence,
                                               const char *scan_turn_full,
                                               const char *scan_tooling_full)
{
    if (!evidence) {
        return;
    }

    if (scan_turn_full && scan_turn_full[0]) {
        append_json_array_unique(evidence, "kernel/turn/turn_entry.c");
        append_json_array_unique(evidence, "kernel/turn/turn_exec.c");
    }
    if (scan_tooling_full && scan_tooling_full[0]) {
        append_json_array_unique(evidence, "kernel/tooling/delegate/delegate_parent_wake.c");
        append_json_array_unique(evidence, "kernel/tooling/delegate/delegate_task_store.c");
    }
}

static void collect_evidence_lines(cJSON *array, const char *text)
{
    const char *section;
    const char *line;

    if (!array || !text || !text[0]) {
        return;
    }
    section = strstr(text, "\n\nEvidence:");
    if (!section) {
        return;
    }
    line = section + strlen("\n\nEvidence:");
    while (*line) {
        const char *end = strchr(line, '\n');
        char cleaned[256];
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char raw[256];

        if (len >= sizeof(raw)) {
            len = sizeof(raw) - 1;
        }
        memcpy(raw, line, len);
        raw[len] = '\0';
        copy_trimmed_line(cleaned, sizeof(cleaned), raw);
        if (!cleaned[0]) {
            break;
        }
        if (strncmp(cleaned, "Dependency result [", strlen("Dependency result [")) == 0 ||
            strcmp(cleaned, "Evidence:") == 0) {
            break;
        }
        if (cleaned[0]) {
            append_json_array_unique(array, cleaned);
        }
        if (!end) {
            break;
        }
        line = end + 1;
    }
}

static void trim_trailing_ascii_space(char *text)
{
    if (!text) {
        return;
    }
    size_t len = strlen(text);
    while (len > 0) {
        char ch = text[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        text[--len] = '\0';
    }
}

static void strip_delegate_prompt_scaffolding_inplace(char *text)
{
    char *marker;

    if (!text || !text[0]) {
        return;
    }

    marker = strstr(text, "\n\nResolved repo root:");
    if (marker) {
        *marker = '\0';
    }

    if (strncmp(text, "Bounded explore override:\n", strlen("Bounded explore override:\n")) == 0) {
        char *requested_scope = strstr(text, "\nRequested scope: ");
        char *rules = strstr(text, "\n\nRules:\n");
        if (requested_scope && rules && requested_scope < rules) {
            requested_scope += strlen("\nRequested scope: ");
            memmove(text, requested_scope, strlen(requested_scope) + 1);
            marker = strstr(text, "\n\nRules:\n");
            if (marker) {
                *marker = '\0';
            }
        }
    }

    trim_trailing_ascii_space(text);
}

void tool_delegate_sanitize_task_key(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }
    for (size_t i = 0; src[i] && out + 1 < dst_size; i++) {
        char ch = src[i];
        if (isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.') {
            dst[out++] = (char)tolower((unsigned char)ch);
        }
    }
    dst[out] = '\0';
}

void tool_delegate_append_dependency_csv(char *dst, size_t dst_size, const char *src)
{
    char clean[DELEGATE_TASK_KEY_LEN];

    if (!dst || dst_size == 0 || !src || !src[0]) {
        return;
    }
    tool_delegate_sanitize_task_key(src, clean, sizeof(clean));
    if (!clean[0]) {
        return;
    }
    if (dst[0]) {
        strlcat(dst, ",", dst_size);
    }
    strlcat(dst, clean, dst_size);
}

bool tool_delegate_coordinator_dependencies_satisfied(const delegate_coordinator_record_t *record,
                                                      const delegate_coordinator_agent_view_t *agent)
{
    if (!record || !agent || !agent->depends_on[0]) {
        return true;
    }
    for (const char *cursor = agent->depends_on; *cursor;) {
        char key[DELEGATE_TASK_KEY_LEN];
        size_t len = 0;

        while (*cursor == ' ' || *cursor == ',') {
            cursor++;
        }
        while (cursor[len] && cursor[len] != ',' && len + 1 < sizeof(key)) {
            key[len] = cursor[len];
            len++;
        }
        key[len] = '\0';
        if (cursor[len] == ',') {
            cursor += len + 1;
        } else {
            cursor += len;
        }
        if (!key[0]) {
            continue;
        }

        bool found_done = false;
        for (int i = 0; i < record->agent_count; i++) {
            const delegate_coordinator_agent_view_t *candidate = &record->agents[i];
            if (strcmp(candidate->task_key, key) == 0 &&
                strcmp(candidate->status, "done") == 0) {
                found_done = true;
                break;
            }
        }
        if (!found_done) {
            return false;
        }
    }
    return true;
}

bool tool_delegate_append_dependency_results_context(const char *coordinator_id,
                                                     const char *depends_on_csv,
                                                     char *dst,
                                                     size_t dst_size)
{
    delegate_coordinator_record_t snapshot;
    bool appended = false;

    if (!coordinator_id || !coordinator_id[0] || !depends_on_csv || !depends_on_csv[0] ||
        !dst || dst_size == 0) {
        return false;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    if (delegate_task_store_snapshot_coordinator(coordinator_id, &snapshot) != 0) {
        return false;
    }

    for (const char *cursor = depends_on_csv; *cursor;) {
        char key[DELEGATE_TASK_KEY_LEN];
        size_t len = 0;

        while (*cursor == ' ' || *cursor == ',') {
            cursor++;
        }
        while (cursor[len] && cursor[len] != ',' && len + 1 < sizeof(key)) {
            key[len] = cursor[len];
            len++;
        }
        key[len] = '\0';
        if (cursor[len] == ',') {
            cursor += len + 1;
        } else {
            cursor += len;
        }
        if (!key[0]) {
            continue;
        }

        for (int i = 0; i < snapshot.agent_count; i++) {
            const delegate_coordinator_agent_view_t *agent = &snapshot.agents[i];
            delegate_task_record_t task_snapshot;
            char clipped[768];
            char preferred[1024];
            const char *source_text = NULL;

            if (strcmp(agent->task_key, key) != 0 || strcmp(agent->status, "done") != 0) {
                continue;
            }
            memset(&task_snapshot, 0, sizeof(task_snapshot));
            memset(preferred, 0, sizeof(preferred));
            if (!delegate_task_store_snapshot_quiet(agent->task_id, &task_snapshot)) {
                break;
            }
            if (delegate_child_session_preferred_visible_text(&task_snapshot,
                                                              preferred,
                                                              sizeof(preferred)) &&
                preferred[0]) {
                source_text = preferred;
            } else if (task_snapshot.output[0]) {
                source_text = task_snapshot.output;
            }
            if (!source_text || !source_text[0]) {
                break;
            }
            tool_delegate_sanitize_summary_text_copy(clipped, sizeof(clipped), source_text);
            if (!clipped[0]) {
                break;
            }
            if (appended) {
                strlcat(dst, "\n\n", dst_size);
            }
            strlcat(dst, "Dependency result [", dst_size);
            strlcat(dst, key, dst_size);
            strlcat(dst, "] ", dst_size);
            strlcat(dst, task_snapshot.description[0] ? task_snapshot.description : agent->description, dst_size);
            strlcat(dst, ":\n", dst_size);
            append_clipped_text(dst, dst_size, clipped, 640);
            appended = true;
            break;
        }
    }

    return appended;
}

bool tool_delegate_try_render_local_dependency_merge(const delegate_request_t *req,
                                                     const char *coordinator_id,
                                                     char *summary,
                                                     size_t summary_size)
{
    char deps[DELEGATE_RESULT_JSON_MAX];
    char safe_prompt[1024];
    char final_summary[1024];

    if (!req || !summary || summary_size == 0 ||
        !coordinator_id || !coordinator_id[0] ||
        strcmp(req->subagent_type, "oracle") != 0 ||
        !req->depends_on[0]) {
        return false;
    }

    deps[0] = '\0';
    if (!tool_delegate_append_dependency_results_context(coordinator_id,
                                                         req->depends_on,
                                                         deps,
                                                         sizeof(deps))) {
        return false;
    }

    tool_delegate_sanitize_summary_text_copy(safe_prompt, sizeof(safe_prompt), req->prompt);
    strip_delegate_prompt_scaffolding_inplace(safe_prompt);
    final_summary[0] = '\0';

    strscpy(final_summary, "依赖子任务合并结果：以下结论只基于已完成依赖任务返回的证据。", sizeof(final_summary));
    if (safe_prompt[0]) {
        strlcat(final_summary, "\n\n任务目标：", sizeof(final_summary));
        append_clipped_text(final_summary, sizeof(final_summary), safe_prompt, 600);
    }
    strlcat(final_summary, "\n\n合并依据：", sizeof(final_summary));
    append_clipped_text(final_summary, sizeof(final_summary), deps, 1200);

    cJSON *root = cJSON_CreateObject();
    cJSON *evidence = cJSON_CreateArray();
    cJSON *risks = cJSON_CreateArray();
    cJSON *next_files = cJSON_CreateArray();
    char *json = NULL;
    bool ok = false;

    if (!root || !evidence || !risks || !next_files) {
        goto done;
    }

    cJSON_AddStringToObject(root, "status", "done");
    cJSON_AddStringToObject(root, "summary", final_summary);
    cJSON_AddItemToObject(root, "evidence", evidence);
    cJSON_AddItemToObject(root, "risks", risks);
    cJSON_AddItemToObject(root, "next_files", next_files);

    collect_evidence_lines(evidence, deps);
    if (cJSON_GetArraySize(evidence) <= 0) {
        append_dependency_default_evidence(evidence, deps, "");
    }
    append_json_array_unique(risks, "当前结果来自依赖子任务的局部输出；如果后续要继续收敛实现入口或改造顺序，还需要补更多目标路径证据。");

    json = cJSON_PrintUnformatted(root);
    if (!json) {
        goto done;
    }
    strscpy(summary, json, summary_size);
    ok = true;

done:
    if (json) {
        free(json);
    }
    cJSON_Delete(root);
    if (!ok) {
        if (summary && summary_size > 0) {
            summary[0] = '\0';
        }
    }
    return ok;
}
