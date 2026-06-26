/* delegate_task 工具：语义化子代理委托层。 */
#include "drivers/tool/tool_bus_view.h"
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_files.h"
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define DELEGATE_RESULT_JSON_MAX 3072

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
    char coordinator_id[16];
    bool run_in_background;
    bool is_batch;
    int batch_count;
    struct {
        char description[64];
        char prompt[2048];
        char subagent_type[24];
    } batch_tasks[DELEGATE_COORDINATOR_AGENTS_MAX];
} delegate_request_t;

static int s_delegate_seq = 0;

static void append_user_message(cJSON *messages, const char *prompt);
static bool read_file_excerpt(const char *path, char *out, size_t out_size);
static bool extract_single_absolute_c_file_path(const char *prompt, char *path, size_t path_size);

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

static void strip_block_between_markers_inplace(char *text,
                                                const char *open_marker,
                                                const char *close_marker)
{
    if (!text || !open_marker || !close_marker) {
        return;
    }

    size_t close_len = strlen(close_marker);
    char *start = strstr(text, open_marker);
    while (start) {
        char *end = strstr(start + strlen(open_marker), close_marker);
        if (!end) {
            *start = '\0';
            trim_trailing_ascii_space(text);
            return;
        }
        end += close_len;
        memmove(start, end, strlen(end) + 1);
        start = strstr(start, open_marker);
    }
}

static void strip_single_line_tag_prefix_inplace(char *text, const char *tag_prefix)
{
    if (!text || !tag_prefix) {
        return;
    }
    char *start = strstr(text, tag_prefix);
    while (start) {
        char *end = strchr(start, '\n');
        if (!end) {
            *start = '\0';
            trim_trailing_ascii_space(text);
            return;
        }
        memmove(start, end + 1, strlen(end + 1) + 1);
        start = strstr(start, tag_prefix);
    }
}

static void strip_inline_transcript_suffix_inplace(char *text)
{
    static const char *const transcript_markers[] = {
        "\nFILE: ",
        "\nSEARCH: ",
        "\n<bash>",
        "\n<fileio>",
        "\n<tool>",
        "\n<read-file",
        "\n```bash",
        "\n```json",
        "\n```shell",
    };

    if (!text) {
        return;
    }

    for (size_t i = 0; i < sizeof(transcript_markers) / sizeof(transcript_markers[0]); i++) {
        char *marker = strstr(text, transcript_markers[i]);
        if (marker) {
            *marker = '\0';
        }
    }
    trim_trailing_ascii_space(text);
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

static bool tool_delegate_text_has_transcript_markup(const char *text)
{
    static const char *const transcript_markers[] = {
        "<bash>",
        "</bash>",
        "<fileio>",
        "</fileio>",
        "<tool>",
        "</tool>",
        "<read-file",
        "```bash",
        "```json",
        "```shell",
        "\nFILE: ",
        "\nSEARCH: ",
        "\nLINES: ",
    };
    return text_has_any_keyword(text, transcript_markers, sizeof(transcript_markers) / sizeof(transcript_markers[0]));
}

#define DELEGATE_INJECTED_FILE_CHAR_BUDGET 2600
#define DELEGATE_FINALIZER_RAW_CHAR_BUDGET 2200
#define DELEGATE_FALLBACK_EXCERPT_CHARS 480

static void append_clipped_text(char *dst, size_t dst_size, const char *src, size_t clip_chars)
{
    if (!dst || dst_size == 0 || !src || !src[0]) {
        return;
    }

    size_t dst_len = strlen(dst);
    if (dst_len >= dst_size - 1) {
        return;
    }

    size_t src_len = strlen(src);
    bool clipped = src_len > clip_chars;
    size_t copy_len = clipped ? clip_chars : src_len;
    size_t remain = dst_size - 1 - dst_len;
    if (copy_len > remain) {
        copy_len = remain;
        clipped = true;
    }

    if (copy_len > 0) {
        memcpy(dst + dst_len, src, copy_len);
        dst[dst_len + copy_len] = '\0';
    }

    if (clipped && strlen(dst) + 32 < dst_size) {
        strlcat(dst, "\n...[truncated by delegate_task]", dst_size);
    }
}

static void compact_injected_file_excerpt(char *text, size_t text_size)
{
    if (!text || !text[0] || text_size == 0) {
        return;
    }

    char *hint = strstr(text, "\n[Hint]");
    if (hint) {
        *hint = '\0';
    }
    if (strlen(text) > DELEGATE_INJECTED_FILE_CHAR_BUDGET) {
        char original[READ_FILE_MAX_CHARS + 1024];
        strscpy(original, text, sizeof(original));
        text[0] = '\0';
        append_clipped_text(text, text_size, original, DELEGATE_INJECTED_FILE_CHAR_BUDGET);
    }
}

static void build_delegate_non_json_failure(const char *final_text,
                                            const char *reasoning_text,
                                            bool tool_budget_exhausted,
                                            bool cancelled,
                                            char *summary,
                                            size_t summary_size)
{
    if (!summary || summary_size == 0) {
        return;
    }

    summary[0] = '\0';
    if (cancelled) {
        strscpy(summary, "delegate_task: subagent cancelled", summary_size);
        return;
    }
    if (tool_budget_exhausted) {
        strscpy(summary,
                "delegate_task: tool iteration budget exhausted before producing a valid JSON result",
                summary_size);
        return;
    }
    if ((final_text && final_text[0] &&
         (tool_delegate_text_has_dsml_markup(final_text) ||
          tool_delegate_text_has_transcript_markup(final_text))) ||
        (reasoning_text && reasoning_text[0] &&
         (tool_delegate_text_has_dsml_markup(reasoning_text) ||
          tool_delegate_text_has_transcript_markup(reasoning_text)))) {
        strscpy(summary,
                "delegate_task: subagent returned tool markup/transcript instead of protocol JSON",
                summary_size);
        return;
    }

    strscpy(summary, "delegate_task: subagent returned non-JSON result after finalizer failed", summary_size);
    if ((final_text && final_text[0]) || (reasoning_text && reasoning_text[0])) {
        strlcat(summary, "\n\nExcerpt:\n", summary_size);
        append_clipped_text(summary,
                            summary_size,
                            (final_text && final_text[0]) ? final_text : reasoning_text,
                            DELEGATE_FALLBACK_EXCERPT_CHARS);
    }
}

bool tool_delegate_parse_result_json_summary(const char *text, char *summary, size_t summary_size)
{
    if (!text || !text[0] || !summary || summary_size == 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(root, "status"));
    const char *json_summary = cJSON_GetStringValue(cJSON_GetObjectItem(root, "summary"));
    cJSON *evidence = cJSON_GetObjectItem(root, "evidence");
    cJSON *risks = cJSON_GetObjectItem(root, "risks");
    cJSON *next_files = cJSON_GetObjectItem(root, "next_files");

    bool ok = status && strcmp(status, "done") == 0 &&
              json_summary && json_summary[0];
    if (!ok) {
        cJSON_Delete(root);
        return false;
    }

    summary[0] = '\0';
    strscpy(summary, json_summary, summary_size);

    if (evidence && cJSON_IsArray(evidence) && cJSON_GetArraySize(evidence) > 0) {
        strlcat(summary, "\n\nEvidence:", summary_size);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, evidence) {
            const char *value = cJSON_GetStringValue(item);
            if (value && value[0]) {
                strlcat(summary, "\n- ", summary_size);
                strlcat(summary, value, summary_size);
            }
        }
    }
    if (risks && cJSON_IsArray(risks) && cJSON_GetArraySize(risks) > 0) {
        strlcat(summary, "\n\nRisks:", summary_size);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, risks) {
            const char *value = cJSON_GetStringValue(item);
            if (value && value[0]) {
                strlcat(summary, "\n- ", summary_size);
                strlcat(summary, value, summary_size);
            }
        }
    }
    if (next_files && cJSON_IsArray(next_files) && cJSON_GetArraySize(next_files) > 0) {
        strlcat(summary, "\n\nNext files:", summary_size);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, next_files) {
            const char *value = cJSON_GetStringValue(item);
            if (value && value[0]) {
                strlcat(summary, "\n- ", summary_size);
                strlcat(summary, value, summary_size);
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

bool tool_delegate_parse_result_json_rendered(const char *text, char *summary, size_t summary_size)
{
    if (!text || !text[0] || !summary || summary_size == 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(root, "status"));
    const char *json_summary = cJSON_GetStringValue(cJSON_GetObjectItem(root, "summary"));
    cJSON *evidence = cJSON_GetObjectItem(root, "evidence");
    cJSON *risks = cJSON_GetObjectItem(root, "risks");
    cJSON *next_files = cJSON_GetObjectItem(root, "next_files");

    if (!status || !status[0] || !json_summary || !json_summary[0]) {
        cJSON_Delete(root);
        return false;
    }

    summary[0] = '\0';
    if (strcmp(status, "done") == 0) {
        strscpy(summary, json_summary, summary_size);
    } else {
        strlcat(summary, "delegate_task: subagent protocol failure", summary_size);
        strlcat(summary, "\n\n", summary_size);
        strlcat(summary, json_summary, summary_size);
    }

    if (evidence && cJSON_IsArray(evidence) && cJSON_GetArraySize(evidence) > 0) {
        strlcat(summary, "\n\nEvidence:", summary_size);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, evidence) {
            const char *value = cJSON_GetStringValue(item);
            if (value && value[0]) {
                strlcat(summary, "\n- ", summary_size);
                strlcat(summary, value, summary_size);
            }
        }
    }
    if (risks && cJSON_IsArray(risks) && cJSON_GetArraySize(risks) > 0) {
        strlcat(summary, "\n\nRisks:", summary_size);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, risks) {
            const char *value = cJSON_GetStringValue(item);
            if (value && value[0]) {
                strlcat(summary, "\n- ", summary_size);
                strlcat(summary, value, summary_size);
            }
        }
    }
    if (next_files && cJSON_IsArray(next_files) && cJSON_GetArraySize(next_files) > 0) {
        strlcat(summary, "\n\nNext files:", summary_size);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, next_files) {
            const char *value = cJSON_GetStringValue(item);
            if (value && value[0]) {
                strlcat(summary, "\n- ", summary_size);
                strlcat(summary, value, summary_size);
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

static bool provider_supports_delegate_result_json(void)
{
    const char *provider = runtime_config_get_active_provider_name();
    if (!provider || !provider[0]) {
        return false;
    }
    return strstr(provider, "deepseek") != NULL ||
           strstr(provider, "moonshot") != NULL ||
           strstr(provider, "kimi") != NULL;
}

static const char *delegate_result_json_provider_name(void)
{
    const char *preferred[] = {"moonshot", "ingenic_local_kimi", "bigmodel"};
    for (size_t i = 0; i < sizeof(preferred) / sizeof(preferred[0]); i++) {
        const char *name = preferred[i];
        const char *model = runtime_config_get_provider_model_for_name(name);
        const char *api_key = runtime_config_get_provider_api_key_for_name(name);
        if (model && model[0] && api_key && api_key[0]) {
            return name;
        }
    }
    if (provider_supports_delegate_result_json()) {
        return runtime_config_get_active_provider_name();
    }
    return NULL;
}

static const char *delegate_subagent_json_provider_name(void)
{
    const char *preferred[] = {"moonshot", "ingenic_local_kimi", "bigmodel", "deepseek_anthropic"};
    for (size_t i = 0; i < sizeof(preferred) / sizeof(preferred[0]); i++) {
        const char *name = preferred[i];
        const char *model = runtime_config_get_provider_model_for_name(name);
        const char *api_key = runtime_config_get_provider_api_key_for_name(name);
        if (model && model[0] && api_key && api_key[0]) {
            return name;
        }
    }
    return delegate_result_json_provider_name();
}

bool tool_delegate_finalize_result_json(const char *subagent_type,
                                        const char *description,
                                        const char *raw_text,
                                        char *summary,
                                        size_t summary_size)
{
    const char *provider_name = delegate_result_json_provider_name();
    if (!provider_name || !raw_text || !raw_text[0] || !summary || summary_size == 0) {
        return false;
    }

    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        return false;
    }

    char clipped_raw[DELEGATE_FINALIZER_RAW_CHAR_BUDGET + 64];
    clipped_raw[0] = '\0';
    append_clipped_text(clipped_raw, sizeof(clipped_raw), raw_text, DELEGATE_FINALIZER_RAW_CHAR_BUDGET);

    char prompt[4096];
    snprintf(prompt, sizeof(prompt),
             "Convert the following subagent result into a strict JSON object.\n"
             "Return JSON only. No markdown fences.\n"
             "Keys required: status, summary, evidence, risks, next_files.\n"
             "Rules:\n"
             "- status must be \"done\" only if the text contains actual findings.\n"
             "- summary must be direct conclusions, not narration of next steps.\n"
             "- evidence/risks/next_files must be arrays of strings.\n"
             "- If the text is only a prelude like 'let me read the file first', set status to \"blocked\" and explain that protocol failed.\n"
             "- Preserve exact file paths and symbol names when present.\n"
             "- Keep summary compact and evidence high-signal.\n"
             "\n"
             "subagent_type: %s\n"
             "description: %s\n"
             "\n"
             "raw_result:\n%s",
             subagent_type ? subagent_type : "",
             description ? description : "",
             clipped_raw);
    append_user_message(messages, prompt);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    err_t err = llm_chat_tools_with_provider_and_format(
        "You are a formatting-only assistant for subagent completion protocol repair.",
        messages,
        NULL,
        provider_name,
        NULL,
        true,
        &resp);
    cJSON_Delete(messages);
    if (err != 0) {
        llm_response_free(&resp);
        return false;
    }

    char final_clean[DELEGATE_RESULT_JSON_MAX];
    char reasoning_clean[DELEGATE_RESULT_JSON_MAX];
    tool_delegate_sanitize_summary_text_copy(final_clean, sizeof(final_clean), resp.text);
    tool_delegate_sanitize_summary_text_copy(reasoning_clean, sizeof(reasoning_clean), resp.reasoning_content);
    bool ok = tool_delegate_parse_result_json_rendered(final_clean, summary, summary_size) ||
              tool_delegate_parse_result_json_rendered(reasoning_clean, summary, summary_size);
    pr_info("delegate_task finalizer: provider=%s ok=%d final_len=%zu reasoning_len=%zu summary_len=%zu",
            provider_name,
            ok ? 1 : 0,
            strlen(final_clean),
            strlen(reasoning_clean),
            ok ? strlen(summary) : 0UL);
    llm_response_free(&resp);
    return ok;
}

static void tool_delegate_sanitize_summary_text_inplace(char *text)
{
    if (!text || !text[0]) {
        return;
    }

    strip_block_between_markers_inplace(text, "<bash>", "</bash>");
    strip_block_between_markers_inplace(text, "<fileio>", "</fileio>");
    strip_block_between_markers_inplace(text, "<tool>", "</tool>");
    strip_single_line_tag_prefix_inplace(text, "<read-file");
    strip_block_between_markers_inplace(text, "```bash", "```");
    strip_block_between_markers_inplace(text, "```shell", "```");
    strip_block_between_markers_inplace(text, "```json", "```");
    strip_inline_transcript_suffix_inplace(text);
    trim_trailing_ascii_space(text);
}

bool tool_delegate_text_has_transcript_markup_public(const char *text)
{
    return tool_delegate_text_has_transcript_markup(text);
}

void tool_delegate_sanitize_summary_text_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }
    strscpy(dst, src, dst_size);
    tool_delegate_sanitize_summary_text_inplace(dst);
}

const char *tool_delegate_safe_output_text(const char *final_text,
                                           const char *reasoning_text,
                                           bool tool_budget_exhausted,
                                           bool cancelled)
{
    static char fallback[1024];
    fallback[0] = '\0';
    build_delegate_non_json_failure(final_text,
                                    reasoning_text,
                                    tool_budget_exhausted,
                                    cancelled,
                                    fallback,
                                    sizeof(fallback));
    return fallback;
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
            "- The caller will use your result directly. Return findings, not a narration of what you plan to do next.\n"
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
            "- If your current draft sounds like 'I will read X next' or 'let me inspect Y first', you are not done yet. Keep using tools until you can state concrete findings.\n"
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
            "- State how the findings help the caller's next decision or next read.\n"
            "- Highlight likely next files to read, key risks, unclear areas, and any notable architecture patterns.\n"
            "- Preferred shape: 1) direct conclusion, 2) evidence with exact paths/symbols, 3) remaining gaps or next files.\n"
            "- Final answer must be valid JSON object, not markdown. Include keys: status, summary, evidence, risks, next_files.\n"
            "- status must be \"done\" only when you are returning findings. summary must contain conclusions, not next-step narration.\n"
            "- Never use a preamble as the final answer. Forbidden final-answer patterns include: '我先看一下', '我们来看一下', 'I will inspect', 'Let me read the file first'.\n"
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
            "- Final answer must be plain natural language only; do not paste raw `FILE:` / `SEARCH:` tool output blocks or shell transcripts.\n"
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
            "- Final answer must be plain natural language only; do not include raw tool transcripts or markup blocks.\n"
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
            "- Final answer must be plain natural language only; do not include raw tool output or command transcript blocks unless explicitly requested.\n"
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
    const char *coordinator_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "coordinator_id"));
    cJSON *run_bg = cJSON_GetObjectItem(root, "run_in_background");
    cJSON *tasks = cJSON_GetObjectItem(root, "tasks");

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
            if (!item_type || !item_type[0] || !item_prompt || !item_prompt[0]) {
                continue;
            }
            if (parse_subagent_kind(item_type) == DELEGATE_SUBAGENT_INVALID) {
                continue;
            }
            int idx = req->batch_count++;
            strscpy(req->batch_tasks[idx].subagent_type, item_type, sizeof(req->batch_tasks[idx].subagent_type));
            strscpy(req->batch_tasks[idx].prompt, item_prompt, sizeof(req->batch_tasks[idx].prompt));
            strscpy(req->batch_tasks[idx].description,
                    item_desc && item_desc[0] ? item_desc : item_type,
                    sizeof(req->batch_tasks[idx].description));
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
    if (parse_subagent_kind(subagent_type) == DELEGATE_SUBAGENT_INVALID) {
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

static void append_user_message(cJSON *messages, const char *prompt)
{
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt ? prompt : "");
    cJSON_AddItemToArray(messages, um);
}

static bool extract_single_absolute_c_file_path(const char *prompt, char *path, size_t path_size)
{
    if (!prompt || !path || path_size == 0) {
        return false;
    }
    path[0] = '\0';

    const char *start = strstr(prompt, "/");
    while (start) {
        const char *end = start;
        while (*end && !isspace((unsigned char)*end) && *end != '"' && *end != '\'' &&
               *end != ',' && *end != ')' && *end != '(') {
            end++;
        }
        size_t len = (size_t)(end - start);
        if (len > 2 && len < path_size &&
            strncmp(end - 2, ".c", 2) == 0) {
            memcpy(path, start, len);
            path[len] = '\0';
            return true;
        }
        start = strstr(end, "/");
    }
    return false;
}

static bool read_file_excerpt(const char *path, char *out, size_t out_size)
{
    if (!path || !path[0] || !out || out_size == 0) {
        return false;
    }

    char input_json[1024];
    snprintf(input_json, sizeof(input_json),
             "{\"path\":\"%s\",\"offset\":1,\"limit\":220}",
             path);
    char *buf = kzalloc(READ_FILE_MAX_CHARS + 1024, GFP_KERNEL);
    if (!buf) {
        return false;
    }
    err_t err = tool_read_file_execute(input_json, buf, READ_FILE_MAX_CHARS + 1024);
    if (err != 0 || !buf[0]) {
        kfree(buf);
        return false;
    }
    strscpy(out, buf, out_size);
    compact_injected_file_excerpt(out, out_size);
    kfree(buf);
    return true;
}

bool tool_delegate_prepare_subagent_prompt(const char *subagent_type,
                                           const char *description,
                                           const char *prompt,
                                           char *prepared_prompt,
                                           size_t prepared_prompt_size,
                                           bool *disable_tools)
{
    if (!prepared_prompt || prepared_prompt_size == 0) {
        return false;
    }
    prepared_prompt[0] = '\0';
    if (disable_tools) {
        *disable_tools = false;
    }

    strscpy(prepared_prompt, prompt ? prompt : "", prepared_prompt_size);
    if (!subagent_type || strcmp(subagent_type, "explore") != 0 || !prompt) {
        return true;
    }

    char path[512];
    if (!extract_single_absolute_c_file_path(prompt, path, sizeof(path))) {
        return true;
    }

    char file_excerpt[READ_FILE_MAX_CHARS + 1024];
    if (!read_file_excerpt(path, file_excerpt, sizeof(file_excerpt))) {
        return true;
    }

    snprintf(prepared_prompt, prepared_prompt_size,
             "%s\n\nYou already have the target file content below. Do not call files/read_file/list first. "
             "Use only the provided content and return findings directly.\n\nProvided file content:\n%s",
             prompt,
             file_excerpt);
    if (disable_tools) {
        *disable_tools = true;
    }
    pr_info("delegate_task prepared subagent prompt: subagent=%s description=%s injected_file=%s disable_tools=1",
            subagent_type ? subagent_type : "-",
            description ? description : "-",
            path);
    return true;
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
    char prepared_prompt[READ_FILE_MAX_CHARS + 4096];
    bool disable_tools = false;
    tool_delegate_prepare_subagent_prompt(req->subagent_type,
                                          req->description,
                                          req->prompt,
                                          prepared_prompt,
                                          sizeof(prepared_prompt),
                                          &disable_tools);

    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        snprintf(output, output_size, "delegate_task: no memory");
        return ERR_NO_MEM;
    }
    append_user_message(messages, prepared_prompt);

    const char *tools_json = disable_tools ? NULL : tool_bus_tools_json_for_channel("websocket");
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    strscpy(msg.channel, "websocket", sizeof(msg.channel));
    snprintf(msg.chat_id, sizeof(msg.chat_id), "delegate_sync_%d", ++s_delegate_seq);
    strscpy(msg.source, "internal", sizeof(msg.source));
    msg.content = prepared_prompt[0] ? strdup(prepared_prompt) : strdup(req->description);
    if (!msg.content) {
        cJSON_Delete(messages);
        snprintf(output, output_size, "delegate_task: no memory");
        return ERR_NO_MEM;
    }

    char *final_text = NULL;
    char *reasoning_text = NULL;
    char final_json_summary[DELEGATE_RESULT_JSON_MAX];
    char reasoning_json_summary[DELEGATE_RESULT_JSON_MAX];
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

    tool_delegate_sanitize_summary_text_inplace(final_text);
    tool_delegate_sanitize_summary_text_inplace(reasoning_text);
    if (tool_delegate_parse_result_json_rendered(final_text, final_json_summary, sizeof(final_json_summary))) {
        strscpy(output, final_json_summary, output_size);
        pr_info("delegate_sync result: subagent=%s source=final_json rendered=1 output_len=%zu",
                req->subagent_type,
                strlen(output));
    } else if (tool_delegate_parse_result_json_rendered(reasoning_text, reasoning_json_summary, sizeof(reasoning_json_summary))) {
        strscpy(output, reasoning_json_summary, output_size);
        pr_info("delegate_sync result: subagent=%s source=reasoning_json rendered=1 output_len=%zu",
                req->subagent_type,
                strlen(output));
    } else if (tool_delegate_finalize_result_json(req->subagent_type,
                                                  req->description,
                                                  final_text && final_text[0] ? final_text : reasoning_text,
                                                  final_json_summary,
                                                  sizeof(final_json_summary))) {
        strscpy(output, final_json_summary, output_size);
        pr_info("delegate_sync result: subagent=%s source=finalizer rendered=1 output_len=%zu",
                req->subagent_type,
                strlen(output));
    } else {
        strscpy(output,
                tool_delegate_safe_output_text(final_text, reasoning_text, tool_budget_exhausted, cancelled),
                output_size);
        pr_info("delegate_sync result: subagent=%s source=safe_fallback rendered=0 output_len=%zu",
                req->subagent_type,
                strlen(output));
    }

    kfree(final_text);
    kfree(reasoning_text);
    return 0;
}

static err_t run_background_subagent(delegate_subagent_kind_t kind,
                                     const delegate_request_t *req,
                                     const char *coordinator_id,
                                     char *output,
                                     size_t output_size)
{
    char task_id[DELEGATE_TASK_ID_LEN];
    snprintf(task_id, sizeof(task_id), "dt_%d", ++s_delegate_seq);

    char prepared_prompt[READ_FILE_MAX_CHARS + 4096];
    bool disable_tools = false;
    tool_delegate_prepare_subagent_prompt(req->subagent_type,
                                          req->description,
                                          req->prompt,
                                          prepared_prompt,
                                          sizeof(prepared_prompt),
                                          &disable_tools);

    cJSON *messages = cJSON_CreateArray();
    append_user_message(messages, prepared_prompt);
    const char *tools_json = disable_tools ? NULL : tool_bus_tools_json_for_channel("websocket");
    const char *json_provider = delegate_subagent_json_provider_name();
    llm_async_chat_t *chat = llm_chat_tools_async_with_provider_and_format(
        subagent_prompt_prefix(kind),
        messages,
        tools_json,
        json_provider,
        subagent_model_for_kind(kind),
        provider_supports_delegate_result_json());
    cJSON_Delete(messages);

    if (!chat) {
        snprintf(output, output_size, "delegate_task: failed to launch background subagent");
        return ERR_FAIL;
    }

    err_t err = delegate_task_store_start(
        task_id,
        coordinator_id,
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

static err_t continue_background_coordinator(const delegate_request_t *req,
                                             char *output,
                                             size_t output_size)
{
    delegate_coordinator_record_t record;
    err_t err = delegate_task_store_poll_coordinator(req->coordinator_id, &record);
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: coordinator_id not found: %s", req->coordinator_id);
        return err;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *agents = cJSON_CreateArray();
    if (!root || !agents) {
        cJSON_Delete(root);
        cJSON_Delete(agents);
        return ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "coordinator_id", record.coordinator_id);
    cJSON_AddStringToObject(root, "status", record.status);
    for (int i = 0; i < record.agent_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "task_id", record.agents[i].task_id);
        cJSON_AddStringToObject(item, "subagent_type", record.agents[i].subagent_type);
        cJSON_AddStringToObject(item, "description", record.agents[i].description);
        cJSON_AddStringToObject(item, "status", record.agents[i].status);
        if (record.agents[i].output[0]) {
            cJSON_AddStringToObject(item, "output", record.agents[i].output);
        }
        if (record.agents[i].target_files[0]) {
            cJSON_AddStringToObject(item, "target_files", record.agents[i].target_files);
        }
        cJSON_AddBoolToObject(item, "write_approved", record.agents[i].write_approved);
        cJSON_AddItemToArray(agents, item);
    }
    cJSON_AddItemToObject(root, "agents", agents);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ERR_NO_MEM;
    }
    strscpy(output, json, output_size);
    kfree(json);
    return 0;
}

static err_t run_background_coordinator(const delegate_request_t *req,
                                        const char *parent_chat_id,
                                        char *output,
                                        size_t output_size)
{
    static const char *implement_target_files_note =
        "\n\nBefore you claim completion, include one line exactly in this format:\n"
        "target_files: <comma-separated absolute or repo-relative paths you changed or intend to change>\n"
        "If you cannot determine target files yet, still include that line with your best current file set.";
    char coordinator_id[DELEGATE_COORDINATOR_ID_LEN];
    snprintf(coordinator_id, sizeof(coordinator_id), "dc_%d", ++s_delegate_seq);

    err_t err = delegate_task_store_start_coordinator(coordinator_id, parent_chat_id);
    if (err != 0) {
        snprintf(output, output_size, "delegate_task: failed to create coordinator");
        return err;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *agents = cJSON_CreateArray();
    if (!root || !agents) {
        cJSON_Delete(root);
        cJSON_Delete(agents);
        return ERR_NO_MEM;
    }

    for (int i = 0; i < req->batch_count; i++) {
        delegate_request_t child;
        memset(&child, 0, sizeof(child));
        strscpy(child.description, req->batch_tasks[i].description, sizeof(child.description));
        if (strcmp(req->batch_tasks[i].subagent_type, "implement") == 0) {
            strscpy(child.prompt, req->batch_tasks[i].prompt, sizeof(child.prompt));
            strlcat(child.prompt, implement_target_files_note, sizeof(child.prompt));
        } else {
            strscpy(child.prompt, req->batch_tasks[i].prompt, sizeof(child.prompt));
        }
        strscpy(child.subagent_type, req->batch_tasks[i].subagent_type, sizeof(child.subagent_type));
        child.run_in_background = true;

        char task_json[4096];
        delegate_subagent_kind_t kind = parse_subagent_kind(child.subagent_type);
        err = run_background_subagent(kind, &child, coordinator_id, task_json, sizeof(task_json));
        if (err != 0) {
            continue;
        }
        cJSON *task_root = cJSON_Parse(task_json);
        const char *task_id = task_root ? cJSON_GetStringValue(cJSON_GetObjectItem(task_root, "task_id")) : NULL;
        if (task_id && task_id[0]) {
            delegate_task_store_attach_task(coordinator_id, task_id);
            cJSON *agent = cJSON_CreateObject();
            cJSON_AddStringToObject(agent, "task_id", task_id);
            cJSON_AddStringToObject(agent, "subagent_type", child.subagent_type);
            cJSON_AddStringToObject(agent, "description", child.description);
            cJSON_AddItemToArray(agents, agent);
        }
        cJSON_Delete(task_root);
    }

    cJSON_AddStringToObject(root, "coordinator_id", coordinator_id);
    cJSON_AddStringToObject(root, "status", "running");
    cJSON_AddItemToObject(root, "agents", agents);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ERR_NO_MEM;
    }
    strscpy(output, json, output_size);
    kfree(json);
    return 0;
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
    if (req.coordinator_id[0]) {
        return continue_background_coordinator(&req, output, output_size);
    }
    if (req.task_id[0]) {
        return continue_background_subagent(&req, output, output_size);
    }

    if (req.is_batch) {
        return run_background_coordinator(&req, parent_chat_id, output, output_size);
    }

    if (req.run_in_background) {
        return run_background_subagent(kind, &req, "", output, output_size);
    }

    return run_sync_single_subagent(kind, &req, parent_chat_id, output, output_size);
}

static struct tool s_delegate_task = {
    .name = "delegate_task",
    .description =
        "Delegate work to a semantic subagent. Use this instead of doing broad discovery yourself."
        " Required: subagent_type + prompt."
        " Supported subagent_type values: explore, librarian, oracle, implement."
        " For 2+ independent subtasks, prefer one batch call with tasks[] so they run under a coordinator."
        " Use run_in_background=true to start a background task and receive a task_id."
        " Use task_id to resume or poll a previously started background subagent."
        " Use tasks[] to start multiple background subagents at once and receive a coordinator_id."
        " Use coordinator_id to poll a previously started delegated batch."
        " When a coordinator batch reaches status=done, summarize directly from agents[].output; do not re-query every child task_id unless you are explicitly resuming one child session."
        " Rules: architecture questions must delegate to oracle; broad repo discovery must delegate to explore; documentation/reference lookup should delegate to librarian; implementation execution may delegate to implement.",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"description\":{\"type\":\"string\",\"description\":\"Short task title\"},"
        "\"prompt\":{\"type\":\"string\",\"description\":\"Full delegated task prompt\"},"
        "\"subagent_type\":{\"type\":\"string\",\"description\":\"One of explore, librarian, oracle, implement\"},"
        "\"run_in_background\":{\"type\":\"boolean\",\"description\":\"true starts a background subagent and returns task_id\"},"
        "\"task_id\":{\"type\":\"string\",\"description\":\"Poll an existing background delegated task\"},"
        "\"coordinator_id\":{\"type\":\"string\",\"description\":\"Poll a background delegated coordinator batch\"},"
        "\"tasks\":{\"type\":\"array\",\"description\":\"Batch background delegated subtasks; each item should include subagent_type, description, prompt\"},"
        "\"load_skills\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Reserved for future skill injection\"},"
        "\"command\":{\"type\":\"string\",\"description\":\"Optional origin command/provenance field\"}"
        "},"
        "\"required\":[]}",
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
