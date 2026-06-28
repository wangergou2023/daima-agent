#include "drivers/tool/tool_delegate_protocol.h"

#include "drivers/llm/llm_proxy.h"
#include "linux/kernel.h"
#include "runtime.h"

#define DELEGATE_RESULT_JSON_MAX 3072
#define DELEGATE_FINALIZER_RAW_CHAR_BUDGET 2200

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
    for (size_t i = 0; i < ARRAY_SIZE(preferred); i++) {
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

bool tool_delegate_subagent_prefers_structured_output(delegate_subagent_kind_t kind)
{
    switch (kind) {
    case DELEGATE_SUBAGENT_EXPLORE:
    case DELEGATE_SUBAGENT_LIBRARIAN:
    case DELEGATE_SUBAGENT_ORACLE:
    case DELEGATE_SUBAGENT_IMPLEMENT:
        return true;
    case DELEGATE_SUBAGENT_INVALID:
    default:
        return false;
    }
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
    cJSON *um;
    snprintf(prompt, sizeof(prompt),
             "Convert the following subagent result into a strict JSON object.\n"
             "Return JSON only. No markdown fences.\n"
             "Keys required: status, summary, evidence, risks, next_files.\n"
             "Rules:\n"
             "- status must be \"done\" only if the text contains actual findings.\n"
             "- summary must be direct conclusions, not narration of next steps.\n"
             "- evidence/risks/next_files must be arrays of strings.\n"
             "- If the text is only a prelude like 'let me read the file first', set status to \"blocked\" and explain that protocol failed.\n"
             "- If the text is tool-result transcript content (for example FILE:/SEARCH:/LINES: blocks), synthesize actual findings from that evidence instead of complaining about the transcript format.\n"
             "- If the text is mostly a path dump or directory listing without explained responsibilities, do not mark it done; either synthesize real structure findings or mark it blocked.\n"
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

    um = cJSON_CreateObject();
    if (!um) {
        cJSON_Delete(messages);
        return false;
    }
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt);
    cJSON_AddItemToArray(messages, um);

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
