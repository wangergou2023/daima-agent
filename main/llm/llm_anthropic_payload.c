#include "llm/llm_anthropic_payload.h"

#include <stdlib.h>
#include <string.h>

#include "agent/context_builder.h"
#include "cJSON.h"
#include "daima_config.h"

static void sanitize_utf8(char *s)
{
    if (!s) return;
    context_fix_truncated_utf8(s, strlen(s));
}

static cJSON *duplicate_tools_anthropic(const char *tools_json)
{
    if (!tools_json) return NULL;
    cJSON *tools = cJSON_Parse(tools_json);
    if (!tools || !cJSON_IsArray(tools)) {
        cJSON_Delete(tools);
        return NULL;
    }
    return tools;
}

static cJSON *convert_messages_anthropic(cJSON *messages)
{
    cJSON *out = cJSON_CreateArray();
    if (!out || !messages || !cJSON_IsArray(messages)) {
        return out;
    }

    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!role || !cJSON_IsString(role) || !content) {
            continue;
        }
        if (strcmp(role->valuestring, "system") == 0) {
            continue;
        }

        cJSON *m = cJSON_CreateObject();
        if (!m) {
            continue;
        }
        cJSON_AddStringToObject(m, "role", role->valuestring);

        if (cJSON_IsString(content)) {
            sanitize_utf8(content->valuestring);
            cJSON_AddStringToObject(m, "content", content->valuestring);
            cJSON_AddItemToArray(out, m);
            continue;
        }

        if (!cJSON_IsArray(content)) {
            cJSON_Delete(m);
            continue;
        }

        cJSON *blocks = cJSON_CreateArray();
        if (!blocks) {
            cJSON_Delete(m);
            continue;
        }

        cJSON *block = NULL;
        cJSON_ArrayForEach(block, content) {
            cJSON *btype = cJSON_GetObjectItem(block, "type");
            if (!btype || !cJSON_IsString(btype)) {
                continue;
            }

            if (strcmp(btype->valuestring, "text") == 0 ||
                strcmp(btype->valuestring, "tool_use") == 0 ||
                strcmp(btype->valuestring, "tool_result") == 0) {
                cJSON *dup = cJSON_Duplicate(block, 1);
                if (dup) {
                    cJSON_AddItemToArray(blocks, dup);
                }
            }
        }

        cJSON_AddItemToObject(m, "content", blocks);
        cJSON_AddItemToArray(out, m);
    }
    return out;
}

cJSON *llm_anthropic_build_tools_body(const char *system_prompt,
                                      cJSON *messages,
                                      const char *tools_json,
                                      const char *model,
                                      int max_tokens)
{
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return NULL;
    }

    cJSON_AddStringToObject(body, "model", model ? model : "");
    cJSON_AddNumberToObject(body, "max_tokens", max_tokens);
    if (system_prompt && system_prompt[0]) {
        cJSON_AddStringToObject(body, "system", system_prompt);
    }
    cJSON_AddItemToObject(body, "messages", convert_messages_anthropic(messages));

    if (tools_json) {
        cJSON *tools = duplicate_tools_anthropic(tools_json);
        if (tools) {
            cJSON_AddItemToObject(body, "tools", tools);
            cJSON *choice = cJSON_CreateObject();
            if (choice) {
                cJSON_AddStringToObject(choice, "type", "auto");
                cJSON_AddItemToObject(body, "tool_choice", choice);
            }
        }
    }
    return body;
}

#ifdef DAIMA_ENABLE_VISION
cJSON *llm_anthropic_build_image_body(const char *system_prompt,
                                      const char *user_text,
                                      const llm_image_content_t *images,
                                      int image_count,
                                      const char *model,
                                      int max_tokens)
{
    (void)images;
    (void)image_count;
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return NULL;
    }
    cJSON_AddStringToObject(body, "model", model ? model : "");
    cJSON_AddNumberToObject(body, "max_tokens", max_tokens);
    if (system_prompt && system_prompt[0]) {
        cJSON_AddStringToObject(body, "system", system_prompt);
    }

    cJSON *messages = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    if (messages && msg) {
        cJSON_AddStringToObject(msg, "role", "user");
        cJSON_AddStringToObject(msg, "content", user_text ? user_text : "");
        cJSON_AddItemToArray(messages, msg);
        cJSON_AddItemToObject(body, "messages", messages);
    } else {
        cJSON_Delete(messages);
        cJSON_Delete(msg);
        cJSON_Delete(body);
        return NULL;
    }
    return body;
}
#endif

daima_err_t llm_anthropic_parse_response(const char *json_text, llm_response_t *resp)
{
    if (!json_text || !resp) {
        return DAIMA_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        return DAIMA_FAIL;
    }

    cJSON *stop = cJSON_GetObjectItem(root, "stop_reason");
    if (stop && cJSON_IsString(stop)) {
        resp->tool_use = strcmp(stop->valuestring, "tool_use") == 0;
    }

    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (content && cJSON_IsArray(content)) {
        cJSON *block = NULL;
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (!type || !cJSON_IsString(type)) {
                continue;
            }

            if (strcmp(type->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text)) {
                    size_t old_len = resp->text_len;
                    size_t add_len = strlen(text->valuestring);
                    char *next = realloc(resp->text, old_len + add_len + 1);
                    if (next) {
                        resp->text = next;
                        memcpy(resp->text + old_len, text->valuestring, add_len);
                        resp->text[old_len + add_len] = '\0';
                        resp->text_len = old_len + add_len;
                    }
                }
                continue;
            }

            if (strcmp(type->valuestring, "tool_use") == 0 &&
                resp->call_count < DAIMA_MAX_TOOL_CALLS) {
                llm_tool_call_t *call = &resp->calls[resp->call_count];
                cJSON *id = cJSON_GetObjectItem(block, "id");
                cJSON *name = cJSON_GetObjectItem(block, "name");
                cJSON *input = cJSON_GetObjectItem(block, "input");
                if (id && cJSON_IsString(id)) {
                    strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
                }
                if (name && cJSON_IsString(name)) {
                    strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
                }
                if (input) {
                    call->input = cJSON_PrintUnformatted(input);
                    if (call->input) {
                        call->input_len = strlen(call->input);
                    }
                }
                resp->call_count++;
                resp->tool_use = true;
            }
        }
    }

    cJSON_Delete(root);
    return DAIMA_OK;
}
