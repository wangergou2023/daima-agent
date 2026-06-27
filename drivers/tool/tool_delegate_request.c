/* delegate_task request parsing helpers */
#include "drivers/tool/tool_delegate_request.h"

#include <string.h>

#include "cjson.h"
#include "drivers/tool/tool_delegate_dependency.h"
#include "drivers/tool/tool_delegate_subagent.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "text.h"

static bool parse_preflight_tool_object(cJSON *item,
                                        char *tool_name,
                                        size_t tool_name_size,
                                        char *input_json,
                                        size_t input_json_size,
                                        bool *continue_on_error)
{
    cJSON *input_item;
    char *encoded = NULL;
    const char *name;

    if (!item || !cJSON_IsObject(item) || !tool_name || tool_name_size == 0 ||
        !input_json || input_json_size == 0 || !continue_on_error) {
        return false;
    }

    name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "tool_name"));
    input_item = cJSON_GetObjectItem(item, "input");
    if (!name || !name[0] || !input_item) {
        return false;
    }

    encoded = cJSON_PrintUnformatted(input_item);
    if (!encoded) {
        return false;
    }

    strscpy(tool_name, name, tool_name_size);
    strscpy(input_json, encoded, input_json_size);
    *continue_on_error = cJSON_IsTrue(cJSON_GetObjectItem(item, "continue_on_error"));
    kfree(encoded);
    return true;
}

err_t tool_delegate_parse_request(const char *input_json,
                                  delegate_request_t *req,
                                  char *output,
                                  size_t output_size)
{
    if (!req || !output || output_size == 0) {
        return ERR_INVALID_ARG;
    }

    memset(req, 0, sizeof(*req));
    cJSON *root = cJSON_Parse(input_json ? input_json : "{}");
    if (!root || !cJSON_IsObject(root)) {
        snprintf(output, output_size, "delegate_task: invalid JSON");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(root, "description"));
    const char *prompt = cJSON_GetStringValue(cJSON_GetObjectItem(root, "prompt"));
    const char *subagent_type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "subagent_type"));
    const char *task_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "task_id"));
    const char *coordinator_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "coordinator_id"));
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    const char *scope = cJSON_GetStringValue(cJSON_GetObjectItem(root, "scope"));
    const char *target_path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "target_path"));
    const char *team_name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "team_name"));
    const char *dispatch_mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "dispatch_mode"));
    cJSON *run_bg = cJSON_GetObjectItem(root, "run_in_background");
    cJSON *tasks = cJSON_GetObjectItem(root, "tasks");
    cJSON *preflight_tool = cJSON_GetObjectItem(root, "preflight_tool");

    strscpy(req->action, action ? action : "", sizeof(req->action));
    strscpy(req->scope, scope ? scope : "", sizeof(req->scope));
    strscpy(req->target_path, target_path ? target_path : "", sizeof(req->target_path));
    strscpy(req->team_name, team_name ? team_name : "", sizeof(req->team_name));
    strscpy(req->dispatch_mode, dispatch_mode ? dispatch_mode : "", sizeof(req->dispatch_mode));
    if (preflight_tool &&
        !parse_preflight_tool_object(preflight_tool,
                                     req->preflight_tool.tool_name,
                                     sizeof(req->preflight_tool.tool_name),
                                     req->preflight_tool.input_json,
                                     sizeof(req->preflight_tool.input_json),
                                     &req->preflight_tool.continue_on_error)) {
        snprintf(output, output_size, "delegate_task: invalid preflight_tool");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    if (tasks && cJSON_IsArray(tasks) && cJSON_GetArraySize(tasks) > 0) {
        req->is_batch = true;
        req->batch_count = 0;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, tasks) {
            if (!cJSON_IsObject(item) || req->batch_count >= DELEGATE_COORDINATOR_AGENTS_MAX) {
                continue;
            }
            const char *item_type = cJSON_GetStringValue(cJSON_GetObjectItem(item, "subagent_type"));
            const char *item_prompt = cJSON_GetStringValue(cJSON_GetObjectItem(item, "prompt"));
            const char *item_desc = cJSON_GetStringValue(cJSON_GetObjectItem(item, "description"));
            const char *item_target_path = cJSON_GetStringValue(cJSON_GetObjectItem(item, "target_path"));
            const char *item_task_key = cJSON_GetStringValue(cJSON_GetObjectItem(item, "task_key"));
            cJSON *depends_on = cJSON_GetObjectItem(item, "depends_on");
            cJSON *item_preflight = cJSON_GetObjectItem(item, "preflight_tool");
            if (!item_type || !item_type[0] || !item_prompt || !item_prompt[0]) {
                continue;
            }
            if (tool_delegate_parse_subagent_kind(item_type) == DELEGATE_SUBAGENT_INVALID) {
                continue;
            }
            int idx = req->batch_count++;
            strscpy(req->batch_tasks[idx].subagent_type, item_type, sizeof(req->batch_tasks[idx].subagent_type));
            strscpy(req->batch_tasks[idx].prompt, item_prompt, sizeof(req->batch_tasks[idx].prompt));
            strscpy(req->batch_tasks[idx].description,
                    item_desc && item_desc[0] ? item_desc : item_type,
                    sizeof(req->batch_tasks[idx].description));
            strscpy(req->batch_tasks[idx].target_path,
                    item_target_path ? item_target_path : "",
                    sizeof(req->batch_tasks[idx].target_path));
            tool_delegate_sanitize_task_key(item_task_key && item_task_key[0] ? item_task_key : item_desc,
                                            req->batch_tasks[idx].task_key,
                                            sizeof(req->batch_tasks[idx].task_key));
            if (cJSON_IsString(depends_on)) {
                tool_delegate_append_dependency_csv(req->batch_tasks[idx].depends_on,
                                                    sizeof(req->batch_tasks[idx].depends_on),
                                                    cJSON_GetStringValue(depends_on));
            } else if (cJSON_IsArray(depends_on)) {
                cJSON *dep = NULL;
                cJSON_ArrayForEach(dep, depends_on) {
                    tool_delegate_append_dependency_csv(req->batch_tasks[idx].depends_on,
                                                        sizeof(req->batch_tasks[idx].depends_on),
                                                        cJSON_GetStringValue(dep));
                }
            }
            if (item_preflight &&
                !parse_preflight_tool_object(item_preflight,
                                             req->batch_tasks[idx].preflight_tool.tool_name,
                                             sizeof(req->batch_tasks[idx].preflight_tool.tool_name),
                                             req->batch_tasks[idx].preflight_tool.input_json,
                                             sizeof(req->batch_tasks[idx].preflight_tool.input_json),
                                             &req->batch_tasks[idx].preflight_tool.continue_on_error)) {
                snprintf(output, output_size, "delegate_task: invalid batch preflight_tool");
                cJSON_Delete(root);
                return ERR_INVALID_ARG;
            }
        }
        if (req->batch_count == 0) {
            snprintf(output, output_size, "delegate_task: batch tasks missing valid subagent_type/prompt");
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        strscpy(req->coordinator_id, coordinator_id ? coordinator_id : "", sizeof(req->coordinator_id));
        cJSON_Delete(root);
        return 0;
    }

    if (task_id && task_id[0]) {
        strscpy(req->task_id, task_id, sizeof(req->task_id));
        strscpy(req->subagent_type, subagent_type ? subagent_type : "", sizeof(req->subagent_type));
        cJSON_Delete(root);
        return 0;
    }

    if (action && action[0]) {
        if (strcmp(action, "list") != 0) {
            snprintf(output, output_size, "delegate_task: unsupported action '%s'", action);
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
        cJSON_Delete(root);
        return 0;
    }

    if (!subagent_type || !subagent_type[0]) {
        if (coordinator_id && coordinator_id[0]) {
            strscpy(req->coordinator_id, coordinator_id, sizeof(req->coordinator_id));
            cJSON_Delete(root);
            return 0;
        }
        snprintf(output, output_size, "delegate_task: missing required field 'subagent_type'");
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }
    if (!task_id || !task_id[0]) {
        if (!prompt || !prompt[0]) {
            snprintf(output, output_size, "delegate_task: missing required field 'prompt'");
            cJSON_Delete(root);
            return ERR_INVALID_ARG;
        }
    }
    if (tool_delegate_parse_subagent_kind(subagent_type) == DELEGATE_SUBAGENT_INVALID) {
        snprintf(output, output_size, "delegate_task: unsupported subagent_type '%s'", subagent_type);
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    strscpy(req->description, description && description[0] ? description : subagent_type, sizeof(req->description));
    strscpy(req->prompt, prompt ? prompt : "", sizeof(req->prompt));
    strscpy(req->subagent_type, subagent_type, sizeof(req->subagent_type));
    strscpy(req->task_id, task_id ? task_id : "", sizeof(req->task_id));
    strscpy(req->coordinator_id, coordinator_id ? coordinator_id : "", sizeof(req->coordinator_id));
    req->run_in_background = cJSON_IsBool(run_bg) ? cJSON_IsTrue(run_bg) : false;

    cJSON_Delete(root);
    return 0;
}
