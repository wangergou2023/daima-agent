/* delegate_task response/session helpers */
#include "drivers/tool/tool_delegate_response.h"

#include "drivers/memory/session_store.h"
#include "drivers/tool/tool_delegate_result_json.h"

#include "cjson.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "text.h"

#include <stdio.h>

err_t tool_delegate_write_json_response(char *output,
                                        size_t output_size,
                                        const char *task_id,
                                        const char *session_id,
                                        const char *status,
                                        const char *delivery,
                                        const char *subagent_type,
                                        const char *description,
                                        const char *model,
                                        const char *payload_output)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        snprintf(output, output_size, "{\"error\":\"no_mem\"}");
        return ERR_NO_MEM;
    }
    if (task_id && task_id[0]) cJSON_AddStringToObject(root, "task_id", task_id);
    if (session_id && session_id[0]) cJSON_AddStringToObject(root, "session_id", session_id);
    if (status && status[0]) cJSON_AddStringToObject(root, "status", status);
    if (delivery && delivery[0]) cJSON_AddStringToObject(root, "delivery", delivery);
    if (subagent_type && subagent_type[0]) cJSON_AddStringToObject(root, "subagent_type", subagent_type);
    if (description && description[0]) cJSON_AddStringToObject(root, "description", description);
    if (model && model[0]) cJSON_AddStringToObject(root, "model", model);
    if (payload_output) cJSON_AddStringToObject(root, "output", payload_output);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        snprintf(output, output_size, "{\"error\":\"encode_failed\"}");
        return ERR_NO_MEM;
    }
    strscpy(output, json, output_size);
    kfree(json);
    return 0;
}

void tool_delegate_persist_turn_session(const char *session_id,
                                        const char *user_prompt,
                                        const char *assistant_text,
                                        const char *reasoning_text)
{
    cJSON *root = NULL;
    char *payload = NULL;

    if (!session_id || !session_id[0] || !assistant_text || !assistant_text[0]) {
        return;
    }

    if (user_prompt && user_prompt[0]) {
        session_store_append(session_id, "user", user_prompt);
    }

    root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "text", assistant_text);
    if (reasoning_text && reasoning_text[0]) {
        cJSON_AddStringToObject(root, "reasoning", reasoning_text);
    }
    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return;
    }
    session_store_append(session_id, "assistant", payload);
    free(payload);
}

const char *tool_delegate_visible_output_or_fallback(const char *raw_output,
                                                     char *visible_output,
                                                     size_t visible_output_size)
{
    if (!visible_output || !visible_output_size) {
        return raw_output ? raw_output : "";
    }
    visible_output[0] = '\0';
    if (!raw_output || !raw_output[0]) {
        return "";
    }
    if (tool_delegate_parse_result_json_rendered(raw_output, visible_output, visible_output_size)) {
        return visible_output;
    }
    return raw_output;
}
