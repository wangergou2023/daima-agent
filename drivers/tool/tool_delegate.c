/* delegate_task 工具：语义化子代理委托层。 */
#include "drivers/tool/tool_bus_view.h"
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_runtime.h"
#include "drivers/channel/gateway/ws_server.h"
#include "drivers/llm/llm_proxy.h"
#include "delegate_task_store.h"
#include "kernel/router.h"
#include "turn_run.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "turn_common.h"
#include "runtime.h"
#include "cjson.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    DELEGATE_SUBAGENT_EXPLORE = 0,
    DELEGATE_SUBAGENT_LIBRARIAN,
    DELEGATE_SUBAGENT_ORACLE,
    DELEGATE_SUBAGENT_IMPLEMENT,
    DELEGATE_SUBAGENT_INVALID,
} delegate_subagent_kind_t;

typedef struct {
    char description[64];
    char prompt[2048];
    char subagent_type[24];
    char task_id[16];
    bool run_in_background;
} delegate_request_t;

static int s_delegate_seq = 0;

static bool text_has_any_keyword(const char *text, const char *const *keywords, size_t count)
{
    if (!text || !text[0]) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (keywords[i] && strstr(text, keywords[i])) {
            return true;
        }
    }
    return false;
}

bool tool_delegate_text_has_dsml_markup(const char *text)
{
    static const char *const dsml_markers[] = {
        "<｜｜DSML｜｜tool_calls>",
        "<｜｜DSML｜｜invoke ",
        "<｜｜DSML｜｜parameter ",
    };
    return text_has_any_keyword(text, dsml_markers, sizeof(dsml_markers) / sizeof(dsml_markers[0]));
}

static const char *delegate_safe_output_text(const char *final_text,
                                             const char *reasoning_text,
                                             bool tool_budget_exhausted,
                                             bool cancelled)
{
    if (cancelled) {
        return "delegate_task: subagent cancelled";
    }
    if (final_text && final_text[0] && !tool_delegate_text_has_dsml_markup(final_text)) {
        return final_text;
    }
    if (reasoning_text && reasoning_text[0] && !tool_delegate_text_has_dsml_markup(reasoning_text)) {
        return reasoning_text;
    }
    if (tool_budget_exhausted) {
        return "delegate_task: tool iteration budget exhausted before producing a valid final summary";
    }
    if (final_text && final_text[0] && tool_delegate_text_has_dsml_markup(final_text)) {
        return "delegate_task: subagent returned invalid tool markup instead of a natural-language summary";
    }
    return "(empty subagent response)";
}

static delegate_subagent_kind_t parse_subagent_kind(const char *subagent_type)
{
    if (!subagent_type || !subagent_type[0]) return DELEGATE_SUBAGENT_INVALID;
    if (strcmp(subagent_type, "explore") == 0) return DELEGATE_SUBAGENT_EXPLORE;
    if (strcmp(subagent_type, "librarian") == 0) return DELEGATE_SUBAGENT_LIBRARIAN;
    if (strcmp(subagent_type, "oracle") == 0) return DELEGATE_SUBAGENT_ORACLE;
    if (strcmp(subagent_type, "implement") == 0) return DELEGATE_SUBAGENT_IMPLEMENT;
    return DELEGATE_SUBAGENT_INVALID;
}

static const char *subagent_prompt_prefix(delegate_subagent_kind_t kind)
{
    switch (kind) {
    case DELEGATE_SUBAGENT_EXPLORE:
        return
            "You are an EXPLORE subagent.\n"
            "\n"
            "Mission:\n"
            "- Focus on repo discovery, impact analysis, code search, architecture surface mapping, and concrete evidence.\n"
            "- Your job is to reduce uncertainty for the caller, not to implement changes.\n"
            "\n"
            "Working style:\n"
            "- Explore broadly first, then narrow down.\n"
            "- Sufficient context is better than exhaustive context.\n"
            "- Once you can identify structure, key modules, and the next files a caller should read, stop exploring and answer.\n"
            "- Prefer listing/searching to find candidate files before deeply reading a smaller set of important files.\n"
            "- Trace relationships in both directions when useful: caller -> callee, definition -> usages, module -> entrypoints.\n"
            "- If the prompt asks for structure or key modules, actively map top-level directories, important subsystems, and their responsibilities.\n"
            "- Do not keep launching new search waves after the same structure is already clear.\n"
            "- For directory structure or repo overview requests, prefer representative sampling over exhaustive traversal.\n"
            "\n"
            "Tool discipline:\n"
            "- Prefer `files action=list/search` to find scope, then `files action=read` for confirmation.\n"
            "- Start with the requested path and its top-level children before descending.\n"
            "- For broad structure requests, cap yourself to a small number of targeted follow-up listings/reads.\n"
            "- Avoid reading large docs or many sibling directories unless they are clearly required to answer the question.\n"
            "- Do not edit files.\n"
            "- Do not call `apply_patch`.\n"
            "- Do not call `delegate_task`.\n"
            "\n"
            "Stop conditions:\n"
            "- Stop when you can name the main entrypoint, major subsystems, and representative files for each important area.\n"
            "- Stop when two consecutive tool rounds would only add more examples rather than changing the answer.\n"
            "- Stop when the likely next files to read are already clear.\n"
            "\n"
            "Output requirements:\n"
            "- Return a concise but concrete discovery summary.\n"
            "- Include exact paths, modules, or symbols as evidence.\n"
            "- Highlight likely next files to read, key risks, unclear areas, and any notable architecture patterns.\n"
            "- Do not give fake certainty. If something is inferred, say it is inferred.";
    case DELEGATE_SUBAGENT_LIBRARIAN:
        return
            "You are a LIBRARIAN subagent.\n"
            "\n"
            "Mission:\n"
            "- Focus on documentation, reference material, configuration guidance, protocol details, and precise factual lookup.\n"
            "- Your job is to gather authoritative answers and convert them into usable guidance for the caller.\n"
            "\n"
            "Working style:\n"
            "- Prefer primary sources inside the repo first: docs, README, AGENTS, config files, schemas, comments, examples.\n"
            "- When comparing options, identify the exact file or config key that supports each conclusion.\n"
            "- Distinguish clearly between documented behavior and your inference.\n"
            "\n"
            "Tool discipline:\n"
            "- Prefer `files action=read/search/list` for local docs and config.\n"
            "- Use `webfetch` only when local material is insufficient and the task explicitly needs outside references.\n"
            "- Do not edit files.\n"
            "- Do not call `apply_patch`.\n"
            "- Do not call `delegate_task`.\n"
            "\n"
            "Output requirements:\n"
            "- Return precise answers with concrete references.\n"
            "- Quote config names, file paths, APIs, fields, or documented constraints exactly when relevant.\n"
            "- Surface contradictions, stale docs, or missing documentation if found.";
    case DELEGATE_SUBAGENT_ORACLE:
        return
            "You are an ORACLE subagent.\n"
            "\n"
            "Mission:\n"
            "- Focus on architecture judgement, contradictions, tradeoffs, failure modes, and recommendation quality.\n"
            "- Your job is to help the caller decide, not merely to restate facts.\n"
            "\n"
            "Working style:\n"
            "- Ground every recommendation in concrete evidence from the codebase or provided context.\n"
            "- Compare at least the obvious viable options when making a recommendation.\n"
            "- Call out hidden costs, coupling, migration risk, and operational failure modes.\n"
            "- Challenge weak assumptions instead of smoothing them over.\n"
            "\n"
            "Tool discipline:\n"
            "- Read enough code and docs to justify a real recommendation.\n"
            "- Do not edit files.\n"
            "- Do not call `apply_patch`.\n"
            "- Do not call `delegate_task`.\n"
            "\n"
            "Output requirements:\n"
            "- Provide a clear recommendation, why it is better, what it costs, and what risks remain.\n"
            "- Separate evidence, judgement, and inference.\n"
            "- If information is insufficient, say what is missing and what would change the decision.";
    case DELEGATE_SUBAGENT_IMPLEMENT:
        return
            "You are an IMPLEMENT subagent.\n"
            "\n"
            "Mission:\n"
            "- Complete the requested implementation task end-to-end using the available tools.\n"
            "- Prefer a correct, coherent fix over a narrow patch that leaves the system inconsistent.\n"
            "\n"
            "Working style:\n"
            "- Read the existing code paths first and align with established patterns before editing.\n"
            "- Narrow the scope, identify the real integration points, then make focused changes.\n"
            "- Avoid speculative edits. Verify assumptions against actual files.\n"
            "- After changes, verify with the most direct available evidence: build, test, grep, or runtime output.\n"
            "\n"
            "Tool discipline:\n"
            "- Use `files` to understand context before editing.\n"
            "- Use `apply_patch` for text edits.\n"
            "- Use `terminal` for build/test/runtime verification when appropriate.\n"
            "- Do not recursively call `delegate_task`.\n"
            "\n"
            "Output requirements:\n"
            "- Summarize what changed, why it changed, and what verification was performed.\n"
            "- If blocked, explain the blocker and the precise missing prerequisite.";
    case DELEGATE_SUBAGENT_INVALID:
    default:
        return "You are a subagent.";
    }
}

static agent_role_t subagent_role_for_kind(delegate_subagent_kind_t kind)
{
    switch (kind) {
    case DELEGATE_SUBAGENT_EXPLORE:
    case DELEGATE_SUBAGENT_LIBRARIAN:
        return AGENT_ROLE_FAST;
    case DELEGATE_SUBAGENT_ORACLE:
        return AGENT_ROLE_ORACLE;
    case DELEGATE_SUBAGENT_IMPLEMENT:
        return AGENT_ROLE_IMPLEMENT;
    case DELEGATE_SUBAGENT_INVALID:
    default:
        return AGENT_ROLE_FAST;
    }
}

static const char *subagent_model_for_kind(delegate_subagent_kind_t kind)
{
    const category_profile_t *profile = category_router_resolve_for_role(subagent_role_for_kind(kind));
    return (profile && profile->model[0]) ? profile->model : llm_get_model_name();
}

static err_t write_delegate_json_response(char *output,
                                          size_t output_size,
                                          const char *task_id,
                                          const char *status,
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
    if (status && status[0]) cJSON_AddStringToObject(root, "status", status);
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

static err_t parse_delegate_request(const char *input_json, delegate_request_t *req, char *output, size_t output_size)
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
    cJSON *run_bg = cJSON_GetObjectItem(root, "run_in_background");

    if (!subagent_type || !subagent_type[0]) {
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
    if (parse_subagent_kind(subagent_type) == DELEGATE_SUBAGENT_INVALID) {
        snprintf(output, output_size, "delegate_task: unsupported subagent_type '%s'", subagent_type);
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    strscpy(req->description, description && description[0] ? description : subagent_type, sizeof(req->description));
    strscpy(req->prompt, prompt ? prompt : "", sizeof(req->prompt));
    strscpy(req->subagent_type, subagent_type, sizeof(req->subagent_type));
    strscpy(req->task_id, task_id ? task_id : "", sizeof(req->task_id));
    req->run_in_background = cJSON_IsBool(run_bg) ? cJSON_IsTrue(run_bg) : false;

    cJSON_Delete(root);
    return 0;
}

static void append_user_message(cJSON *messages, const char *prompt)
{
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt ? prompt : "");
    cJSON_AddItemToArray(messages, um);
}

static bool request_is_bounded_explore_overview(const delegate_request_t *req)
{
    static const char *const broad_keywords[] = {
        "bounded exploration request",
        "broad discovery",
        "目录结构", "代码组织", "关键模块", "仓库结构", "项目结构",
        "top-level structure", "directory structure", "repo structure",
        "code organization", "key modules", "important modules"
    };

    if (!req) {
        return false;
    }

    return text_has_any_keyword(req->prompt, broad_keywords, sizeof(broad_keywords) / sizeof(broad_keywords[0])) ||
           text_has_any_keyword(req->description, broad_keywords, sizeof(broad_keywords) / sizeof(broad_keywords[0]));
}

static int sync_subagent_tool_budget(delegate_subagent_kind_t kind, const delegate_request_t *req)
{
    if (kind != DELEGATE_SUBAGENT_EXPLORE || !req) {
        return 0;
    }

    if (request_is_bounded_explore_overview(req)) {
        return 3;
    }

    return 0;
}

static err_t run_sync_single_subagent(delegate_subagent_kind_t kind,
                                      const delegate_request_t *req,
                                      const char *parent_chat_id,
                                      char *output,
                                      size_t output_size)
{
    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        snprintf(output, output_size, "delegate_task: no memory");
        return ERR_NO_MEM;
    }
    append_user_message(messages, req->prompt);

    const char *tools_json = tool_bus_tools_json_for_channel("websocket");
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    snprintf(msg.chat_id, sizeof(msg.chat_id), "delegate_sync_%d", ++s_delegate_seq);
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.content = req->prompt[0] ? strdup(req->prompt) : strdup(req->description);
    if (!msg.content) {
        cJSON_Delete(messages);
        snprintf(output, output_size, "delegate_task: no memory");
        return ERR_NO_MEM;
    }

    char *final_text = NULL;
    char *reasoning_text = NULL;
    int iteration = 0;
    bool tool_budget_exhausted = false;
    bool cancelled = false;
    int max_tool_iterations = sync_subagent_tool_budget(kind, req);
    if (parent_chat_id && parent_chat_id[0]) {
        ws_server_send_subagent_event(parent_chat_id,
                                      "subagent_start",
                                      req->subagent_type,
                                      req->description,
                                      max_tool_iterations > 0 ? "bounded" : "unbounded");
    }
    pr_info("delegate_sync start: chat=%s subagent=%s max_tool_iterations=%d description=%s",
            msg.chat_id,
            req->subagent_type[0] ? req->subagent_type : "-",
            max_tool_iterations,
            req->description[0] ? req->description : "-");
    err_t err = agent_turn_run(
        subagent_prompt_prefix(kind),
        messages,
        tools_json,
        &msg,
        subagent_model_for_kind(kind),
        max_tool_iterations,
        0,
        &final_text,
        &reasoning_text,
        &iteration,
        &tool_budget_exhausted,
        &cancelled);
    pr_info("delegate_sync done: chat=%s err=%s iterations=%d exhausted=%d cancelled=%d",
            msg.chat_id,
            err_name(err),
            iteration,
            tool_budget_exhausted ? 1 : 0,
            cancelled ? 1 : 0);
    if (parent_chat_id && parent_chat_id[0]) {
        char detail[128];
        snprintf(detail, sizeof(detail),
                 "iterations=%d%s%s",
                 iteration,
                 tool_budget_exhausted ? " · budget_exhausted" : "",
                 cancelled ? " · cancelled" : "");
        ws_server_send_subagent_event(parent_chat_id,
                                      "subagent_done",
                                      req->subagent_type,
                                      req->description,
                                      detail);
    }

    cJSON_Delete(messages);
    kfree(msg.content);

    if (err != 0) {
        snprintf(output, output_size, "delegate_task: subagent failed: %s", err_name(err));
        kfree(final_text);
        kfree(reasoning_text);
        return err;
    }

    strscpy(output,
            delegate_safe_output_text(final_text, reasoning_text, tool_budget_exhausted, cancelled),
            output_size);

    kfree(final_text);
    kfree(reasoning_text);
    return 0;
}

static err_t run_background_subagent(delegate_subagent_kind_t kind,
                                     const delegate_request_t *req,
                                     char *output,
                                     size_t output_size)
{
    char task_id[DELEGATE_TASK_ID_LEN];
    snprintf(task_id, sizeof(task_id), "dt_%d", ++s_delegate_seq);

    cJSON *messages = cJSON_CreateArray();
    append_user_message(messages, req->prompt);
    const char *tools_json = tool_bus_tools_json_for_channel("websocket");
    llm_async_chat_t *chat = llm_chat_tools_async(
        subagent_prompt_prefix(kind),
        messages,
        tools_json,
        subagent_model_for_kind(kind));
    cJSON_Delete(messages);

    if (!chat) {
        snprintf(output, output_size, "delegate_task: failed to launch background subagent");
        return ERR_FAIL;
    }

    err_t err = delegate_task_store_start(
        task_id,
        req->subagent_type,
        req->description,
        subagent_model_for_kind(kind),
        chat);
    if (err != 0) {
        llm_chat_async_free(chat);
        snprintf(output, output_size, "delegate_task: failed to persist background task");
        return err;
    }

    return write_delegate_json_response(output, output_size, task_id, "running",
                                        req->subagent_type, req->description,
                                        subagent_model_for_kind(kind), "");
}

static err_t continue_background_subagent(const delegate_request_t *req,
                                          char *output,
                                          size_t output_size)
{
    delegate_task_record_t record;
    err_t err = delegate_task_store_poll(req->task_id, &record);
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: task_id not found: %s", req->task_id);
        return err;
    }

    const char *status = "running";
    if (record.status == DELEGATE_TASK_DONE) status = "done";
    else if (record.status == DELEGATE_TASK_FAILED) status = "failed";

    return write_delegate_json_response(output, output_size, record.task_id, status,
                                        record.subagent_type, record.description,
                                        record.model,
                                        record.output[0] ? record.output : "");
}

static err_t delegate_task_execute(const char *input_json,
                                   char *output, size_t output_size)
{
    delegate_request_t req;
    const struct message *current_msg = tool_runtime_current_message();
    const char *parent_chat_id = current_msg ? current_msg->chat_id : "";
    err_t err = parse_delegate_request(input_json, &req, output, output_size);
    if (err != 0) {
        return err;
    }

    delegate_subagent_kind_t kind = parse_subagent_kind(req.subagent_type);
    if (req.task_id[0]) {
        return continue_background_subagent(&req, output, output_size);
    }

    if (req.run_in_background) {
        return run_background_subagent(kind, &req, output, output_size);
    }

    return run_sync_single_subagent(kind, &req, parent_chat_id, output, output_size);
}

static struct tool s_delegate_task = {
    .name = "delegate_task",
    .description =
        "Delegate work to a semantic subagent. Use this instead of doing broad discovery yourself."
        " Required: subagent_type + prompt."
        " Supported subagent_type values: explore, librarian, oracle, implement."
        " Use run_in_background=true to start a background task and receive a task_id."
        " Use task_id to poll a previously started background subagent."
        " Rules: architecture questions must delegate to oracle; broad repo discovery must delegate to explore; documentation/reference lookup should delegate to librarian; implementation execution may delegate to implement.",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"description\":{\"type\":\"string\",\"description\":\"Short task title\"},"
        "\"prompt\":{\"type\":\"string\",\"description\":\"Full delegated task prompt\"},"
        "\"subagent_type\":{\"type\":\"string\",\"description\":\"One of explore, librarian, oracle, implement\"},"
        "\"run_in_background\":{\"type\":\"boolean\",\"description\":\"true starts a background subagent and returns task_id\"},"
        "\"task_id\":{\"type\":\"string\",\"description\":\"Poll an existing background delegated task\"},"
        "\"load_skills\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Reserved for future skill injection\"},"
        "\"command\":{\"type\":\"string\",\"description\":\"Optional origin command/provenance field\"}"
        "},"
        "\"required\":[\"subagent_type\"]}",
    .execute = delegate_task_execute,
};

static int delegate_task_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_driver s_delegate_task_driver = {
    .drv.name = "delegate_task",
    .drv.probe = delegate_task_tool_probe,
    .execute = delegate_task_execute,
};

const struct tool *tool_delegate_definition(void)
{
    delegate_task_store_init();
    return &s_delegate_task;
}

const struct tool_driver *tool_delegate_driver(void)
{
    return &s_delegate_task_driver;
}
