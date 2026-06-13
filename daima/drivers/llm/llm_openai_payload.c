#include "drivers/llm/llm_openai_payload.h"
#include "context_build.h"

#include <stdlib.h>
#include <string.h>

#include "linux/printk.h"
#include "linux/slab.h"

static const char *TAG = "llm_parse";

static void log_tool_call_parse(const char *protocol,
                                int index,
                                const char *id,
                                const char *name,
                                const char *input_json)
{
    char preview[320];
    const char *state = "value";
    const char *src = input_json ? input_json : "";
    size_t n = input_json ? strlen(input_json) : 0;
    if (!input_json) {
        state = "null";
    } else if (strcmp(input_json, "{}") == 0) {
        state = "empty_object";
    }
    size_t shown = n > sizeof(preview) - 1 ? sizeof(preview) - 1 : n;
    memcpy(preview, src, shown);
    preview[shown] = '\0';
    for (size_t i = 0; i < shown; i++) {
        if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
            preview[i] = ' ';
        }
    }
    DAIMA_LOGI(TAG,
               "%s parsed tool_call[%d]: id=%s name=%s input_state=%s input_len=%u input=%s%s",
               protocol,
               index,
               id && id[0] ? id : "<missing>",
               name && name[0] ? name : "<missing>",
               state,
               (unsigned)n,
               preview[0] ? preview : "<empty>",
               n > shown ? "..." : "");
}

/* Sanitize a string in-place, stripping invalid UTF-8 sequences */
static void sanitize_utf8(char *s)
{
    if (!s) return;
    size_t len = strlen(s);
    context_fix_truncated_utf8(s, len);
}

static cJSON *convert_tools_openai(const char *tools_json)
{
    if (!tools_json) return NULL;
    cJSON *arr = cJSON_Parse(tools_json);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return NULL;
    }

    cJSON *out = cJSON_CreateArray();
    cJSON *tool = NULL;
    cJSON_ArrayForEach(tool, arr) {
        cJSON *name = cJSON_GetObjectItem(tool, "name");
        cJSON *desc = cJSON_GetObjectItem(tool, "description");
        cJSON *schema = cJSON_GetObjectItem(tool, "input_schema");
        if (!name || !cJSON_IsString(name)) {
            continue;
        }

        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", name->valuestring);
        if (desc && cJSON_IsString(desc)) {
            cJSON_AddStringToObject(func, "description", desc->valuestring);
        }
        if (schema) {
            cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(schema, 1));
        }

        cJSON *wrap = cJSON_CreateObject();
        cJSON_AddStringToObject(wrap, "type", "function");
        cJSON_AddItemToObject(wrap, "function", func);
        cJSON_AddItemToArray(out, wrap);
    }

    cJSON_Delete(arr);
    return out;
}

static cJSON *convert_messages_openai(const char *system_prompt, cJSON *messages, bool add_reasoning_content)
{
    cJSON *out = cJSON_CreateArray();
    if (system_prompt && system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(out, sys);
    }

    if (!messages || !cJSON_IsArray(messages)) {
        return out;
    }

    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!role || !cJSON_IsString(role)) {
            continue;
        }

        if (content && cJSON_IsString(content)) {
            sanitize_utf8(content->valuestring);
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", role->valuestring);
            cJSON_AddStringToObject(m, "content", content->valuestring);
            cJSON_AddItemToArray(out, m);
            continue;
        }

        if (!content || !cJSON_IsArray(content)) {
            continue;
        }

        if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", "assistant");

            char *text_buf = NULL;
            char *reasoning_buf = NULL;
            size_t off = 0;
            cJSON *block = NULL;
            cJSON *tool_calls = NULL;
            cJSON_ArrayForEach(block, content) {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        size_t tlen = strlen(text->valuestring);
                        char *tmp = realloc(text_buf, off + tlen + 1);
                        if (tmp) {
                            text_buf = tmp;
                            memcpy(text_buf + off, text->valuestring, tlen);
                            off += tlen;
                            text_buf[off] = '\0';
                        }
                    }
                } else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "reasoning") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text) && text->valuestring[0]) {
                        kfree(reasoning_buf);
                        reasoning_buf = strdup(text->valuestring);
                    }
                } else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "tool_use") == 0) {
                    if (!tool_calls) tool_calls = cJSON_CreateArray();
                    cJSON *id = cJSON_GetObjectItem(block, "id");
                    cJSON *name = cJSON_GetObjectItem(block, "name");
                    cJSON *input = cJSON_GetObjectItem(block, "input");
                    if (!name || !cJSON_IsString(name)) {
                        continue;
                    }

                    cJSON *tc = cJSON_CreateObject();
                    if (id && cJSON_IsString(id)) {
                        cJSON_AddStringToObject(tc, "id", id->valuestring);
                    }
                    cJSON_AddStringToObject(tc, "type", "function");

                    cJSON *func = cJSON_CreateObject();
                    cJSON_AddStringToObject(func, "name", name->valuestring);
                    if (input) {
                        char *args = cJSON_PrintUnformatted(input);
                        if (args) {
                            cJSON_AddStringToObject(func, "arguments", args);
                            kfree(args);
                        }
                    }
                    cJSON_AddItemToObject(tc, "function", func);
                    cJSON_AddItemToArray(tool_calls, tc);
                }
            }

            sanitize_utf8(text_buf);
            cJSON_AddStringToObject(m, "content", text_buf ? text_buf : "");
            if (tool_calls) {
                cJSON_AddItemToObject(m, "tool_calls", tool_calls);
            }
            if (add_reasoning_content && reasoning_buf && reasoning_buf[0]) {
                cJSON_AddStringToObject(m, "reasoning_content", reasoning_buf);
            }
            cJSON_AddItemToArray(out, m);
            kfree(text_buf);
            kfree(reasoning_buf);
            continue;
        }

        if (strcmp(role->valuestring, "user") != 0) {
            continue;
        }

        cJSON *block = NULL;
        bool has_user_text = false;
        bool has_user_image = false;
        char *text_buf = NULL;
        size_t off = 0;
        cJSON *user_content = NULL;
        cJSON_ArrayForEach(block, content) {
            cJSON *btype = cJSON_GetObjectItem(block, "type");
            if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "tool_result") == 0) {
                cJSON *tool_id = cJSON_GetObjectItem(block, "tool_use_id");
                cJSON *tcontent = cJSON_GetObjectItem(block, "content");
                if (!tool_id || !cJSON_IsString(tool_id)) {
                    continue;
                }
                cJSON *tm = cJSON_CreateObject();
                cJSON_AddStringToObject(tm, "role", "tool");
                cJSON_AddStringToObject(tm, "tool_call_id", tool_id->valuestring);
                if (tcontent && cJSON_IsString(tcontent)) {
                    sanitize_utf8(tcontent->valuestring);
                    cJSON_AddStringToObject(tm, "content", tcontent->valuestring);
                } else {
                    cJSON_AddStringToObject(tm, "content", "");
                }
                cJSON_AddItemToArray(out, tm);
            } else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "image_url") == 0) {
                if (!user_content) {
                    user_content = cJSON_CreateArray();
                }
                if (user_content) {
                    cJSON *dup = cJSON_Duplicate(block, 1);
                    if (dup) {
                        cJSON_AddItemToArray(user_content, dup);
                        has_user_image = true;
                    }
                }
            } else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text)) {
                    size_t tlen = strlen(text->valuestring);
                    char *tmp = realloc(text_buf, off + tlen + 1);
                    if (tmp) {
                        text_buf = tmp;
                        memcpy(text_buf + off, text->valuestring, tlen);
                        off += tlen;
                        text_buf[off] = '\0';
                    }
                    has_user_text = true;
                }
            }
        }

        if (!has_user_image && user_content) {
            cJSON_Delete(user_content);
            user_content = NULL;
        }

        if (has_user_image) {
            if (has_user_text && user_content) {
                cJSON *tb = cJSON_CreateObject();
                if (tb) {
                    cJSON_AddStringToObject(tb, "type", "text");
                    cJSON_AddStringToObject(tb, "text", text_buf ? text_buf : "");
                    cJSON_AddItemToArray(user_content, tb);
                }
            }
            if (user_content && cJSON_GetArraySize(user_content) > 0) {
                cJSON *um = cJSON_CreateObject();
                cJSON_AddStringToObject(um, "role", "user");
                cJSON_AddItemToObject(um, "content", user_content);
                cJSON_AddItemToArray(out, um);
                user_content = NULL;
            }
            if (user_content) {
                cJSON_Delete(user_content);
            }
        } else if (has_user_text) {
            sanitize_utf8(text_buf);
            cJSON *um = cJSON_CreateObject();
            cJSON_AddStringToObject(um, "role", "user");
            cJSON_AddStringToObject(um, "content", text_buf ? text_buf : "");
            cJSON_AddItemToArray(out, um);
        }
        kfree(text_buf);
    }

    return out;
}

cJSON *llm_openai_build_tools_body(const char *system_prompt,
                                   cJSON *messages,
                                   const char *tools_json,
                                   const char *model,
                                   int max_completion_tokens,
                                   bool use_max_tokens_field,
                                   bool disable_thinking,
                                   const char *reasoning_effort,
                                   bool add_reasoning_content)
{
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return NULL;
    }

    cJSON_AddStringToObject(body, "model", model ? model : "");
    cJSON_AddNumberToObject(
        body,
        use_max_tokens_field ? "max_tokens" : "max_completion_tokens",
        max_completion_tokens);
    if (disable_thinking) {
        cJSON *thinking = cJSON_CreateObject();
        cJSON_AddStringToObject(thinking, "type", "disabled");
        cJSON_AddItemToObject(body, "thinking", thinking);
    } else if (reasoning_effort && reasoning_effort[0]) {
        cJSON *thinking = cJSON_CreateObject();
        cJSON_AddStringToObject(thinking, "type", "enabled");
        cJSON_AddItemToObject(body, "thinking", thinking);
        cJSON_AddStringToObject(body, "reasoning_effort", reasoning_effort);
    }

    cJSON_AddItemToObject(body, "messages",
                          convert_messages_openai(system_prompt, messages, add_reasoning_content));

    if (tools_json) {
        cJSON *tools = convert_tools_openai(tools_json);
        if (tools) {
            cJSON_AddItemToObject(body, "tools", tools);
            cJSON_AddStringToObject(body, "tool_choice", "auto");
        }
    }

    return body;
}

#ifdef DAIMA_ENABLE_VISION
cJSON *llm_create_multimodal_content(const char *text, const llm_image_content_t *images, int image_count)
{
    if (!images || image_count <= 0) {
        return NULL;
    }

    cJSON *content_array = cJSON_CreateArray();
    if (!content_array) {
        return NULL;
    }

    for (int i = 0; i < image_count; i++) {
        if (!images[i].image_data) {
            continue;
        }

        cJSON *image_block = cJSON_CreateObject();
        if (!image_block) {
            continue;
        }

        cJSON_AddStringToObject(image_block, "type", "image_url");
        cJSON *image_url_obj = cJSON_CreateObject();
        if (image_url_obj) {
            char *url_data = kmalloc(images[i].image_data_len + 64, GFP_KERNEL);
            if (url_data) {
                snprintf(url_data,
                         images[i].image_data_len + 64,
                         "data:%s;base64,%s",
                         images[i].mime_type,
                         images[i].image_data);
                cJSON_AddStringToObject(image_url_obj, "url", url_data);
                kfree(url_data);
            }
            cJSON_AddItemToObject(image_block, "image_url", image_url_obj);
        }
        cJSON_AddItemToArray(content_array, image_block);
    }

    if (text && text[0]) {
        cJSON *text_block = cJSON_CreateObject();
        if (text_block) {
            cJSON_AddStringToObject(text_block, "type", "text");
            cJSON_AddStringToObject(text_block, "text", text);
            cJSON_AddItemToArray(content_array, text_block);
        }
    }

    return content_array;
}

cJSON *llm_openai_build_image_body(const char *system_prompt,
                                   const char *user_text,
                                   const llm_image_content_t *images,
                                   int image_count,
                                   const char *model,
                                   int max_completion_tokens,
                                   bool use_max_tokens_field,
                                   bool disable_thinking,
                                   const char *reasoning_effort)
{
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return NULL;
    }

    cJSON_AddStringToObject(body, "model", model ? model : "");
    cJSON_AddNumberToObject(
        body,
        use_max_tokens_field ? "max_tokens" : "max_completion_tokens",
        max_completion_tokens);
    if (disable_thinking) {
        cJSON *thinking = cJSON_CreateObject();
        cJSON_AddStringToObject(thinking, "type", "disabled");
        cJSON_AddItemToObject(body, "thinking", thinking);
    } else if (reasoning_effort && reasoning_effort[0]) {
        cJSON *thinking = cJSON_CreateObject();
        cJSON_AddStringToObject(thinking, "type", "enabled");
        cJSON_AddItemToObject(body, "thinking", thinking);
        cJSON_AddStringToObject(body, "reasoning_effort", reasoning_effort);
    }

    cJSON *messages = cJSON_CreateArray();
    if (system_prompt && system_prompt[0]) {
        cJSON *sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", system_prompt);
        cJSON_AddItemToArray(messages, sys_msg);
    }

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON *content = llm_create_multimodal_content(user_text, images, image_count);
    if (!content) {
        cJSON_Delete(user_msg);
        cJSON_Delete(messages);
        cJSON_Delete(body);
        return NULL;
    }
    cJSON_AddItemToObject(user_msg, "content", content);
    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(body, "messages", messages);
    return body;
}
#endif

daima_err_t llm_openai_parse_response(const char *json_text, llm_response_t *resp)
{
    if (!json_text || !resp) {
        return DAIMA_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        return DAIMA_FAIL;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    cJSON *choice0 = choices && cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    if (choice0) {
        cJSON *finish = cJSON_GetObjectItem(choice0, "finish_reason");
        if (finish && cJSON_IsString(finish)) {
            resp->tool_use = strcmp(finish->valuestring, "tool_calls") == 0;
        }

        cJSON *message = cJSON_GetObjectItem(choice0, "message");
        if (message) {
            cJSON *content = cJSON_GetObjectItem(message, "content");
            if (content && cJSON_IsString(content)) {
                size_t tlen = strlen(content->valuestring);
                resp->text = kzalloc(tlen + 1, GFP_KERNEL);
                if (resp->text) {
                    memcpy(resp->text, content->valuestring, tlen);
                    resp->text_len = tlen;
                }
            }

            cJSON *reasoning = cJSON_GetObjectItem(message, "reasoning_content");
            if (reasoning && cJSON_IsString(reasoning) && reasoning->valuestring[0]) {
                size_t rlen = strlen(reasoning->valuestring);
                resp->reasoning_content = kzalloc(rlen + 1, GFP_KERNEL);
                if (resp->reasoning_content) {
                    memcpy(resp->reasoning_content, reasoning->valuestring, rlen);
                    resp->reasoning_content_len = rlen;
                }
            }

            cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
            if (tool_calls && cJSON_IsArray(tool_calls)) {
                cJSON *tc = NULL;
                cJSON_ArrayForEach(tc, tool_calls) {
                    if (resp->call_count >= DAIMA_MAX_TOOL_CALLS) {
                        break;
                    }

                    llm_tool_call_t *call = &resp->calls[resp->call_count];
                    cJSON *id = cJSON_GetObjectItem(tc, "id");
                    cJSON *func = cJSON_GetObjectItem(tc, "function");
                    if (id && cJSON_IsString(id)) {
                        strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
                    }
                    if (func) {
                        cJSON *name = cJSON_GetObjectItem(func, "name");
                        cJSON *args = cJSON_GetObjectItem(func, "arguments");
                        if (name && cJSON_IsString(name)) {
                            strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
                        }
                        if (args && cJSON_IsString(args)) {
                            call->input = strdup(args->valuestring);
                            if (call->input) {
                                call->input_len = strlen(call->input);
                            }
                        }
                    }
                    log_tool_call_parse("openai",
                                        resp->call_count,
                                        call->id,
                                        call->name,
                                        call->input);
                    resp->call_count++;
                }
                if (resp->call_count > 0) {
                    resp->tool_use = true;
                }
            }
        }
    }

    cJSON_Delete(root);
    return DAIMA_OK;
}
