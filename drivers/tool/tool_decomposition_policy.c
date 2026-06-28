#include "drivers/tool/tool_decomposition_policy.h"

#include <string.h>

#include "kernel/intent.h"

static bool text_contains_any(const char *text, const char *const *keywords, size_t count)
{
    if (!text || !text[0] || !keywords || count == 0) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (keywords[i] && keywords[i][0] && strstr(text, keywords[i])) {
            return true;
        }
    }
    return false;
}

static bool message_explicitly_disallows_multi_subagents(const struct message *msg)
{
    static const char *const deny_keywords[] = {
        "不要并行", "不用并行", "不必并行", "不要拆分", "不要分步骤", "不要安排多个subagent",
        "不要安排多个 subagent", "不要多个subagent", "不要多个 subagent", "不要多个子代理",
        "不要多 subagent", "自己处理", "直接自己做",
        "do not parallel", "don't parallel", "do not split", "don't split",
        "do not use multiple subagents", "don't use multiple subagents"
    };

    return msg && text_contains_any(msg->content,
                                    deny_keywords,
                                    sizeof(deny_keywords) / sizeof(deny_keywords[0]));
}

static int count_list_separators(const char *content)
{
    int count = 0;
    const char *cursor = content;

    if (!content || !content[0]) {
        return 0;
    }

    while ((cursor = strstr(cursor, "、")) != NULL) {
        count++;
        cursor += strlen("、");
    }
    cursor = content;
    while ((cursor = strstr(cursor, "以及")) != NULL) {
        count++;
        cursor += strlen("以及");
    }
    cursor = content;
    while ((cursor = strstr(cursor, " and ")) != NULL) {
        count++;
        cursor += strlen(" and ");
    }
    return count;
}

static bool message_has_explicit_multi_target_shape(const struct message *msg)
{
    static const char *const multi_keywords[] = {
        "分别", "各自", "同时", "并行", "拆成", "拆分", "多个", "几项", "几部分",
        "multiple", "separately", "independently", "in parallel", "split into",
        "several", "multiple targets"
    };

    if (!msg || !msg->content || !msg->content[0]) {
        return false;
    }

    return count_list_separators(msg->content) >= 1 ||
           text_contains_any(msg->content,
                             multi_keywords,
                             sizeof(multi_keywords) / sizeof(multi_keywords[0]));
}

static bool message_has_shared_context_signal(const struct message *msg)
{
    static const char *const shared_keywords[] = {
        "整体", "全局", "统一", "综合", "汇总", "串起来", "协作关系", "交互关系", "依赖关系",
        "主流程", "调用链", "数据流", "状态流", "模块职责", "取舍", "方案", "设计",
        "overall", "global", "unified", "synthesize", "synthesis", "tradeoff",
        "interaction", "dependency", "main flow", "call flow", "data flow",
        "state flow", "how it works", "design"
    };

    return msg && text_contains_any(msg->content,
                                    shared_keywords,
                                    sizeof(shared_keywords) / sizeof(shared_keywords[0]));
}

static bool message_has_ordering_signal(const struct message *msg)
{
    static const char *const ordering_keywords[] = {
        "先", "然后", "再", "最后", "步骤", "分阶段", "一步一步", "顺序",
        "first", "then", "after that", "finally", "step by step", "staged", "sequence"
    };

    return msg && text_contains_any(msg->content,
                                    ordering_keywords,
                                    sizeof(ordering_keywords) / sizeof(ordering_keywords[0]));
}

static bool message_looks_like_complex_work(const struct message *msg)
{
    static const char *const complexity_keywords[] = {
        "分析", "设计", "实现", "修复", "排查", "调查", "规划", "方案", "重构", "迁移",
        "analyze", "design", "implement", "fix", "debug", "investigate", "plan", "refactor", "migrate"
    };

    return msg && text_contains_any(msg->content,
                                    complexity_keywords,
                                    sizeof(complexity_keywords) / sizeof(complexity_keywords[0]));
}

const char *tool_decomposition_mode_name(tool_decomposition_mode_t mode)
{
    switch (mode) {
    case TOOL_DECOMP_NONE:
        return "none";
    case TOOL_DECOMP_SERIAL:
        return "serial";
    case TOOL_DECOMP_PARALLEL:
        return "parallel";
    default:
        return "unknown";
    }
}

tool_decomposition_mode_t tool_decomposition_policy_classify_message(const struct message *msg)
{
    bool multi_target;
    bool shared_context;
    bool ordering;
    bool complex_work;

    if (!msg || !msg->content || !msg->content[0]) {
        return TOOL_DECOMP_NONE;
    }
    if (strcmp(msg->source, MSG_SOURCE_DELEGATE) == 0) {
        return TOOL_DECOMP_NONE;
    }
    if (message_explicitly_disallows_multi_subagents(msg)) {
        return TOOL_DECOMP_NONE;
    }

    multi_target = message_has_explicit_multi_target_shape(msg);
    shared_context = message_has_shared_context_signal(msg);
    ordering = message_has_ordering_signal(msg);
    complex_work = message_looks_like_complex_work(msg) || msg->intent != INTENT_QA;

    if (!complex_work) {
        return TOOL_DECOMP_NONE;
    }
    if (ordering) {
        return TOOL_DECOMP_SERIAL;
    }
    if (multi_target && !shared_context) {
        return TOOL_DECOMP_PARALLEL;
    }
    if (multi_target || shared_context) {
        return TOOL_DECOMP_SERIAL;
    }
    return TOOL_DECOMP_NONE;
}

bool tool_decomposition_policy_requires_delegate_only(const struct message *msg)
{
    return tool_decomposition_policy_classify_message(msg) != TOOL_DECOMP_NONE;
}

bool tool_decomposition_policy_prefers_parallel(const struct message *msg)
{
    return tool_decomposition_policy_classify_message(msg) == TOOL_DECOMP_PARALLEL;
}
