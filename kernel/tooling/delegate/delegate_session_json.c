#include "delegate_session_json.h"

#include <stdio.h>
#include <string.h>

#include "drivers/tool/tool_delegate_result_json.h"
#include "drivers/memory/session_store.h"

typedef struct {
    cJSON *array;
    int count;
    int total;
    bool truncated;
    bool replay_reset;
    double first_seq;
    double last_seq;
} delegate_child_session_history_snapshot_t;

static double json_array_first_seq(cJSON *array)
{
    cJSON *item = NULL;
    cJSON *seq = NULL;

    if (!array || !cJSON_IsArray(array) || cJSON_GetArraySize(array) <= 0) {
        return 0;
    }
    item = cJSON_GetArrayItem(array, 0);
    seq = item ? cJSON_GetObjectItemCaseSensitive(item, "seq") : NULL;
    return cJSON_IsNumber(seq) ? seq->valuedouble : 0;
}

static double json_array_last_seq(cJSON *array)
{
    cJSON *item = NULL;
    cJSON *seq = NULL;
    int size = 0;

    if (!array || !cJSON_IsArray(array) || cJSON_GetArraySize(array) <= 0) {
        return 0;
    }
    size = cJSON_GetArraySize(array);
    item = cJSON_GetArrayItem(array, size - 1);
    seq = item ? cJSON_GetObjectItemCaseSensitive(item, "seq") : NULL;
    return cJSON_IsNumber(seq) ? seq->valuedouble : 0;
}

static void filter_json_array_after_seq(cJSON *array, unsigned long after_seq, bool *replay_reset_out)
{
    bool replay_reset = false;
    bool found_after = (after_seq == 0);
    int idx = 0;

    if (!array || !cJSON_IsArray(array)) {
        if (replay_reset_out) {
            *replay_reset_out = false;
        }
        return;
    }

    if (after_seq > 0) {
        cJSON *first = cJSON_GetArrayItem(array, 0);
        cJSON *first_seq = first ? cJSON_GetObjectItemCaseSensitive(first, "seq") : NULL;
        if (cJSON_IsNumber(first_seq) && first_seq->valuedouble > 0 &&
            (unsigned long)first_seq->valuedouble > after_seq + 1UL) {
            replay_reset = true;
            found_after = true;
        }
    }

    while (idx < cJSON_GetArraySize(array)) {
        cJSON *item = cJSON_GetArrayItem(array, idx);
        cJSON *seq = item ? cJSON_GetObjectItemCaseSensitive(item, "seq") : NULL;
        unsigned long seq_value = cJSON_IsNumber(seq) && seq->valuedouble > 0
                                      ? (unsigned long)seq->valuedouble
                                      : 0UL;

        if (!found_after) {
            if (seq_value > after_seq) {
                found_after = true;
                idx++;
                continue;
            }
            cJSON_DeleteItemFromArray(array, idx);
            continue;
        }
        idx++;
    }

    if (replay_reset_out) {
        *replay_reset_out = replay_reset;
    }
}

static const char *delegate_render_visible_text(const char *text,
                                                char *rendered,
                                                size_t rendered_size)
{
    if (!text || !text[0]) {
        return "";
    }
    if (rendered && rendered_size > 0 &&
        tool_delegate_parse_result_json_rendered(text, rendered, rendered_size)) {
        return rendered;
    }
    return text;
}

static bool delegate_is_compaction_summary_text(const char *text)
{
    return text &&
           strncmp(text, "[上下文压缩摘要]", strlen("[上下文压缩摘要]")) == 0;
}

static void delegate_history_make_id(char *buf,
                                     size_t size,
                                     const char *session_id,
                                     const char *role,
                                     const char *source,
                                     const char *content,
                                     double ts,
                                     int ordinal)
{
    unsigned long hash = 2166136261u;
    const unsigned char *ptr = NULL;
    const char *parts[] = {
        session_id ? session_id : "",
        role ? role : "",
        source ? source : "",
        content ? content : "",
    };

    if (!buf || size == 0) {
        return;
    }

    for (size_t part_idx = 0; part_idx < sizeof(parts) / sizeof(parts[0]); part_idx++) {
        for (ptr = (const unsigned char *)parts[part_idx]; ptr && *ptr; ptr++) {
            hash ^= (unsigned long)(*ptr);
            hash *= 16777619u;
        }
        hash ^= (unsigned long)'|';
        hash *= 16777619u;
    }

    snprintf(buf, size, "hist-%s-%lld-%d-%08lx",
             session_id && session_id[0] ? session_id : "delegate",
             (long long)ts,
             ordinal,
             hash);
}

static void append_pending_queue_item(cJSON *array,
                                      const char *request_type,
                                      const char *request_id,
                                      const char *prompt)
{
    cJSON *item = NULL;

    if (!array || !request_type || !request_type[0] || !prompt || !prompt[0]) {
        return;
    }

    item = cJSON_CreateObject();
    if (!item) {
        return;
    }

    cJSON_AddStringToObject(item, "request_type", request_type);
    cJSON_AddStringToObject(item, "request_id", request_id ? request_id : "");
    cJSON_AddStringToObject(item, "prompt", prompt);
    cJSON_AddItemToArray(array, item);
}

static void append_pending_queue_items_from_session(cJSON *array,
                                                    const delegate_session_pending_request_t *items,
                                                    int count,
                                                    const char *fallback_type)
{
    if (!array || !items || count <= 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        const char *request_type = items[i].request_type[0] ? items[i].request_type : fallback_type;
        append_pending_queue_item(array,
                                  request_type,
                                  items[i].request_id,
                                  items[i].prompt_text);
    }
}

static void append_pending_request_object(cJSON *parent,
                                          const char *request_type,
                                          const char *request_id,
                                          const char *prompt)
{
    cJSON *item = NULL;

    if (!parent || !request_type || !request_type[0] || !prompt || !prompt[0]) {
        return;
    }

    item = cJSON_CreateObject();
    if (!item) {
        return;
    }

    cJSON_AddStringToObject(item, "request_type", request_type);
    cJSON_AddStringToObject(item, "request_id", request_id ? request_id : "");
    cJSON_AddStringToObject(item, "prompt", prompt);
    cJSON_AddItemToObject(parent, "pending_request", item);
}

static void append_latest_frame_object(cJSON *parent, const delegate_child_session_view_t *session)
{
    const delegate_session_frame_t *frame = NULL;
    cJSON *item = NULL;

    if (!parent || !session || session->frame_count <= 0) {
        return;
    }

    frame = &session->frames[session->frame_count - 1];
    item = cJSON_CreateObject();
    if (!item) {
        return;
    }
    if (frame->id[0]) {
        cJSON_AddStringToObject(item, "id", frame->id);
    }
    cJSON_AddNumberToObject(item, "seq", (double)frame->seq);

    cJSON_AddStringToObject(item, "type", frame->type);
    cJSON_AddStringToObject(item, "phase", frame->phase);
    cJSON_AddStringToObject(item, "status", frame->status);
    cJSON_AddStringToObject(item, "task", frame->task);
    cJSON_AddStringToObject(item, "detail", frame->detail);
    if (frame->output_preview[0]) {
        cJSON_AddStringToObject(item, "output_preview", frame->output_preview);
    }
    if (frame->blocker_kind[0]) {
        cJSON_AddStringToObject(item, "blocker_kind", frame->blocker_kind);
    }
    if (frame->blocker_text[0]) {
        cJSON_AddStringToObject(item, "blocker_text", frame->blocker_text);
    }
    cJSON_AddNumberToObject(item, "ts", frame->ts_ms);
    cJSON_AddItemToObject(parent, "latest_frame", item);
}

static int count_child_session_history_messages(const char *session_id)
{
    char path[256];
    FILE *f = NULL;
    char line[16384];
    int count = 0;

    if (!session_id || !session_id[0]) {
        return 0;
    }
    if (session_store_artifact_path(session_id,
                                    SESSION_ARTIFACT_HISTORY,
                                    path,
                                    sizeof(path)) != 0) {
        return 0;
    }

    f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        cJSON *obj = NULL;
        cJSON *role = NULL;
        cJSON *content = NULL;
        const char *content_text = NULL;

        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        if (!line[0]) {
            continue;
        }

        obj = cJSON_Parse(line);
        if (!obj) {
            continue;
        }
        role = cJSON_GetObjectItemCaseSensitive(obj, "role");
        content = cJSON_GetObjectItemCaseSensitive(obj, "content");
        content_text = cJSON_IsString(content) ? content->valuestring : NULL;
        if (cJSON_IsString(role) &&
            cJSON_IsString(content) &&
            !delegate_is_compaction_summary_text(content_text)) {
            count++;
        }
        cJSON_Delete(obj);
    }

    fclose(f);
    return count;
}

static delegate_child_session_history_snapshot_t build_child_session_history_snapshot(const char *session_id,
                                                                                     int max_msgs,
                                                                                     unsigned long after_seq)
{
    delegate_child_session_history_snapshot_t snapshot = {0};
    char history_json[DELEGATE_CHILD_SESSION_HISTORY_BUF_SIZE];
    cJSON *messages = NULL;
    cJSON *msg = NULL;

    if (!session_id || !session_id[0]) {
        snapshot.array = cJSON_CreateArray();
        return snapshot;
    }

    history_json[0] = '\0';
    if (session_store_get_history_json(session_id,
                                       history_json,
                                       sizeof(history_json),
                                       max_msgs > 0 ? max_msgs : DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT) != 0) {
        snapshot.array = cJSON_CreateArray();
        return snapshot;
    }

    messages = cJSON_Parse(history_json);
    snapshot.array = cJSON_CreateArray();
    if (!messages || !cJSON_IsArray(messages) || !snapshot.array) {
        cJSON_Delete(messages);
        cJSON_Delete(snapshot.array);
        snapshot.array = cJSON_CreateArray();
        return snapshot;
    }

    int ordinal = 0;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItemCaseSensitive(msg, "role");
        cJSON *content = cJSON_GetObjectItemCaseSensitive(msg, "content");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(msg, "id");
        cJSON *seq = cJSON_GetObjectItemCaseSensitive(msg, "seq");
        cJSON *ts = cJSON_GetObjectItemCaseSensitive(msg, "ts");
        cJSON *source = cJSON_GetObjectItemCaseSensitive(msg, "source");
        cJSON *item = NULL;
        const char *text_value = NULL;
        const char *reasoning_value = NULL;
        char rendered_text[1024];
        char generated_id[96];
        const char *resolved_id = NULL;
        double resolved_seq = 0;

        if (!cJSON_IsString(role) || !cJSON_IsString(content)) {
            continue;
        }
        ordinal++;

        item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        if (cJSON_IsString(id) && id->valuestring && id->valuestring[0]) {
            resolved_id = id->valuestring;
        } else {
            delegate_history_make_id(generated_id,
                                     sizeof(generated_id),
                                     session_id,
                                     role->valuestring,
                                     cJSON_IsString(source) ? source->valuestring : "",
                                     content->valuestring,
                                     cJSON_IsNumber(ts) ? ts->valuedouble : 0,
                                     ordinal);
            resolved_id = generated_id;
        }
        if (resolved_id && resolved_id[0]) {
            cJSON_AddStringToObject(item, "id", resolved_id);
        }
        resolved_seq = (cJSON_IsNumber(seq) && seq->valuedouble > 0)
                           ? seq->valuedouble
                           : (double)ordinal;
        cJSON_AddNumberToObject(item, "seq", resolved_seq);
        cJSON_AddStringToObject(item, "role", role->valuestring);
        if (cJSON_IsNumber(ts)) {
            cJSON_AddNumberToObject(item, "ts", ts->valuedouble);
        }
        if (cJSON_IsString(source) && source->valuestring && source->valuestring[0]) {
            cJSON_AddStringToObject(item, "source", source->valuestring);
        }

        if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON *parsed = cJSON_Parse(content->valuestring);
            if (parsed && cJSON_IsObject(parsed)) {
                cJSON *text = cJSON_GetObjectItemCaseSensitive(parsed, "text");
                cJSON *reasoning = cJSON_GetObjectItemCaseSensitive(parsed, "reasoning");
                if (cJSON_IsString(text)) {
                    text_value = text->valuestring;
                }
                if (cJSON_IsString(reasoning) && reasoning->valuestring[0]) {
                    reasoning_value = reasoning->valuestring;
                }
            }
            if (!text_value) {
                text_value = content->valuestring;
            }
            cJSON_AddStringToObject(item,
                                    "content",
                                    delegate_render_visible_text(text_value,
                                                                 rendered_text,
                                                                 sizeof(rendered_text)));
            if (reasoning_value && reasoning_value[0]) {
                cJSON_AddStringToObject(item, "reasoning", reasoning_value);
            }
            cJSON_Delete(parsed);
        } else {
            cJSON_AddStringToObject(item,
                                    "content",
                                    delegate_render_visible_text(content->valuestring,
                                                                 rendered_text,
                                                                 sizeof(rendered_text)));
        }

        cJSON_AddItemToArray(snapshot.array, item);
    }

    cJSON_Delete(messages);
    snapshot.total = count_child_session_history_messages(session_id);
    filter_json_array_after_seq(snapshot.array, after_seq, &snapshot.replay_reset);
    snapshot.count = cJSON_GetArraySize(snapshot.array);
    if (snapshot.total < snapshot.count) {
        snapshot.total = snapshot.count;
    }
    snapshot.truncated = snapshot.total > snapshot.count;
    snapshot.first_seq = json_array_first_seq(snapshot.array);
    snapshot.last_seq = json_array_last_seq(snapshot.array);
    return snapshot;
}

static bool seq_window_replay_reset(unsigned long after_seq,
                                    unsigned long first_seq,
                                    unsigned long seq_next)
{
    if (after_seq == 0 || first_seq == 0) {
        return false;
    }
    if (seq_next > 0 && after_seq >= seq_next - 1) {
        return false;
    }
    return first_seq > after_seq + 1UL;
}

static void filter_frame_array_after_seq(cJSON *array, unsigned long after_seq)
{
    int idx = 0;

    if (!array || !cJSON_IsArray(array) || after_seq == 0) {
        return;
    }

    while (idx < cJSON_GetArraySize(array)) {
        cJSON *item = cJSON_GetArrayItem(array, idx);
        cJSON *seq = item ? cJSON_GetObjectItemCaseSensitive(item, "seq") : NULL;
        unsigned long seq_value = cJSON_IsNumber(seq) && seq->valuedouble > 0
                                      ? (unsigned long)seq->valuedouble
                                      : 0UL;
        if (seq_value > after_seq) {
            idx++;
            continue;
        }
        cJSON_DeleteItemFromArray(array, idx);
    }
}

cJSON *delegate_child_session_json_build_from_task(const delegate_task_record_t *task_snapshot,
                                                   const delegate_child_session_json_options_t *options)
{
    const delegate_child_session_view_t *session = NULL;
    char rendered_summary[1024];
    cJSON *child = NULL;
    cJSON *commits = NULL;
    cJSON *frames = NULL;
    cJSON *pending_queue = NULL;
    cJSON *permissions = NULL;
    cJSON *questions = NULL;
    cJSON *window = NULL;
    delegate_child_session_history_snapshot_t history_snapshot = {0};
    int history_limit = DELEGATE_CHILD_SESSION_HISTORY_LIMIT_DEFAULT;

    if (!task_snapshot) {
        return NULL;
    }

    if (options && options->history_limit > 0) {
        history_limit = options->history_limit;
    }

    session = &task_snapshot->child_session;
    child = cJSON_CreateObject();
    commits = cJSON_CreateArray();
    frames = cJSON_CreateArray();
    pending_queue = cJSON_CreateObject();
    permissions = cJSON_CreateArray();
    questions = cJSON_CreateArray();
    window = cJSON_CreateObject();
    history_snapshot = build_child_session_history_snapshot(task_snapshot->session_id,
                                                            history_limit,
                                                            options ? options->history_after_seq : 0);
    if (!child || !commits || !frames || !pending_queue || !permissions || !questions ||
        !window || !history_snapshot.array) {
        cJSON_Delete(child);
        cJSON_Delete(commits);
        cJSON_Delete(frames);
        cJSON_Delete(pending_queue);
        cJSON_Delete(permissions);
        cJSON_Delete(questions);
        cJSON_Delete(window);
        cJSON_Delete(history_snapshot.array);
        return NULL;
    }

    cJSON_AddStringToObject(child,
                            "summary",
                            delegate_render_visible_text(session->summary,
                                                         rendered_summary,
                                                         sizeof(rendered_summary)));
    cJSON_AddStringToObject(child, "status",
                            task_snapshot->status == DELEGATE_TASK_DONE
                                ? "done"
                                : task_snapshot->status == DELEGATE_TASK_FAILED
                                    ? "failed"
                                    : task_snapshot->status == DELEGATE_TASK_QUEUED
                                        ? "queued"
                                        : "running");
    cJSON_AddItemToObject(child, "history", history_snapshot.array);

    for (int i = 0; i < session->commit_count; i++) {
        const delegate_session_commit_t *commit_view = &session->commits[i];
        char rendered_commit_text[512];
        cJSON *commit = cJSON_CreateObject();
        if (!commit) {
            continue;
        }
        if (commit_view->id[0]) {
            cJSON_AddStringToObject(commit, "id", commit_view->id);
        }
        cJSON_AddNumberToObject(commit, "seq", (double)commit_view->seq);
        cJSON_AddStringToObject(commit, "kind", commit_view->kind);
        cJSON_AddStringToObject(commit, "phase", commit_view->phase);
        cJSON_AddStringToObject(commit, "status", commit_view->status);
        cJSON_AddStringToObject(commit, "label", commit_view->label);
        cJSON_AddStringToObject(commit,
                                "text",
                                delegate_render_visible_text(commit_view->text,
                                                             rendered_commit_text,
                                                             sizeof(rendered_commit_text)));
        cJSON_AddNumberToObject(commit, "ts", commit_view->ts_ms);
        cJSON_AddItemToArray(commits, commit);
    }
    if (options && options->commit_after_seq > 0) {
        filter_frame_array_after_seq(commits, options->commit_after_seq);
    }
    cJSON_AddItemToObject(child, "commits", commits);

    for (int i = 0; i < session->frame_count; i++) {
        const delegate_session_frame_t *frame_view = &session->frames[i];
        cJSON *frame = cJSON_CreateObject();
        if (!frame) {
            continue;
        }
        if (frame_view->id[0]) {
            cJSON_AddStringToObject(frame, "id", frame_view->id);
        }
        cJSON_AddNumberToObject(frame, "seq", (double)frame_view->seq);
        cJSON_AddStringToObject(frame, "type", frame_view->type);
        cJSON_AddStringToObject(frame, "phase", frame_view->phase);
        cJSON_AddStringToObject(frame, "status", frame_view->status);
        cJSON_AddStringToObject(frame, "task", frame_view->task);
        cJSON_AddStringToObject(frame, "detail", frame_view->detail);
        if (frame_view->output_preview[0]) {
            cJSON_AddStringToObject(frame, "output_preview", frame_view->output_preview);
        }
        if (frame_view->blocker_kind[0]) {
            cJSON_AddStringToObject(frame, "blocker_kind", frame_view->blocker_kind);
        }
        if (frame_view->blocker_text[0]) {
            cJSON_AddStringToObject(frame, "blocker_text", frame_view->blocker_text);
        }
        cJSON_AddNumberToObject(frame, "ts", frame_view->ts_ms);
        cJSON_AddItemToArray(frames, frame);
    }
    if (options && options->frame_after_seq > 0) {
        filter_frame_array_after_seq(frames, options->frame_after_seq);
    }
    cJSON_AddItemToObject(child, "frames", frames);
    append_latest_frame_object(child, session);

    if (task_snapshot->pending_request.request_type[0] &&
        task_snapshot->pending_request.prompt_text[0] &&
        (strcmp(task_snapshot->pending_request.request_type, "question") == 0 ||
         strcmp(task_snapshot->pending_request.request_type, "question_text") == 0)) {
        append_pending_request_object(child,
                                      task_snapshot->pending_request.request_type,
                                      task_snapshot->pending_request.request_id,
                                      task_snapshot->pending_request.prompt_text);
        append_pending_queue_item(questions,
                                  task_snapshot->pending_request.request_type,
                                  task_snapshot->pending_request.request_id,
                                  task_snapshot->pending_request.prompt_text);
    } else if (task_snapshot->pending_request.request_type[0] &&
               task_snapshot->pending_request.prompt_text[0]) {
        append_pending_request_object(child,
                                      task_snapshot->pending_request.request_type,
                                      task_snapshot->pending_request.request_id,
                                      task_snapshot->pending_request.prompt_text);
        append_pending_queue_item(permissions,
                                  task_snapshot->pending_request.request_type,
                                  task_snapshot->pending_request.request_id,
                                  task_snapshot->pending_request.prompt_text);
    } else {
        append_pending_queue_items_from_session(permissions,
                                                session->permissions,
                                                session->permission_count,
                                                "permission");
        append_pending_queue_items_from_session(questions,
                                                session->questions,
                                                session->question_count,
                                                "question");
    }
    cJSON_AddItemToObject(pending_queue, "permissions", permissions);
    cJSON_AddItemToObject(pending_queue, "questions", questions);
    cJSON_AddItemToObject(child, "pending_queue", pending_queue);

    cJSON_AddNumberToObject(window, "history_limit", history_limit);
    cJSON_AddNumberToObject(window, "history_count", history_snapshot.count);
    cJSON_AddNumberToObject(window, "history_total", history_snapshot.total);
    cJSON_AddBoolToObject(window, "history_truncated", history_snapshot.truncated);
    cJSON_AddNumberToObject(window, "history_after_seq", options ? (double)options->history_after_seq : 0.0);
    cJSON_AddNumberToObject(window, "history_first_seq", history_snapshot.first_seq);
    cJSON_AddNumberToObject(window, "history_last_seq", history_snapshot.last_seq);
    cJSON_AddNumberToObject(window, "frame_limit", DELEGATE_SESSION_FRAME_LIMIT);
    cJSON_AddNumberToObject(window, "frame_count", cJSON_GetArraySize(frames));
    cJSON_AddNumberToObject(window,
                            "frame_total",
                            session->frame_seq_next > 0 ? (double)(session->frame_seq_next - 1) : 0.0);
    cJSON_AddBoolToObject(window,
                          "frame_truncated",
                          session->frame_seq_next > 0 &&
                              (int)(session->frame_seq_next - 1) > session->frame_count);
    cJSON_AddNumberToObject(window, "frame_after_seq", options ? (double)options->frame_after_seq : 0.0);
    cJSON_AddNumberToObject(window,
                            "frame_first_seq",
                            json_array_first_seq(frames));
    cJSON_AddNumberToObject(window,
                            "frame_last_seq",
                            json_array_last_seq(frames));
    cJSON_AddNumberToObject(window, "commit_limit", DELEGATE_SESSION_COMMIT_LIMIT);
    cJSON_AddNumberToObject(window, "commit_count", cJSON_GetArraySize(commits));
    cJSON_AddNumberToObject(window,
                            "commit_total",
                            session->commit_seq_next > 0 ? (double)(session->commit_seq_next - 1) : 0.0);
    cJSON_AddBoolToObject(window,
                          "commit_truncated",
                          session->commit_seq_next > 0 &&
                              (int)(session->commit_seq_next - 1) > session->commit_count);
    cJSON_AddNumberToObject(window, "commit_after_seq", options ? (double)options->commit_after_seq : 0.0);
    cJSON_AddNumberToObject(window,
                            "commit_first_seq",
                            json_array_first_seq(commits));
    cJSON_AddNumberToObject(window,
                            "commit_last_seq",
                            json_array_last_seq(commits));
    cJSON_AddBoolToObject(window,
                          "replay_reset",
                          history_snapshot.replay_reset ||
                              seq_window_replay_reset(options ? options->frame_after_seq : 0,
                                                      (unsigned long)json_array_first_seq(frames),
                                                      session->frame_seq_next) ||
                              seq_window_replay_reset(options ? options->commit_after_seq : 0,
                                                      (unsigned long)json_array_first_seq(commits),
                                                      session->commit_seq_next));
    cJSON_AddItemToObject(child, "window", window);

    return child;
}
