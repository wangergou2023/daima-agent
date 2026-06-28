#include "drivers/tool/tool_delegate_batch_policy.h"

#include <stddef.h>
#include <string.h>

static bool text_contains_any(const char *text, const char *const *keywords, size_t keyword_count)
{
    if (!text || !text[0]) {
        return false;
    }
    for (size_t i = 0; i < keyword_count; i++) {
        if (keywords[i] && strstr(text, keywords[i])) {
            return true;
        }
    }
    return false;
}

bool tool_delegate_prompt_prefers_unified_repo_analysis(const char *prompt,
                                                        const char *description)
{
    static const char *const synthesis_keywords[] = {
        "代码架构", "代码框架", "架构", "主流程", "执行流程", "调用链", "数据流", "状态流",
        "模块职责", "职责拆分", "协作关系", "交互关系", "依赖关系", "整体流程", "整体工作流",
        "主链路", "阅读顺序", "入口与主流程", "不要只看目录名",
        "architecture", "architectural", "main flow", "execution flow", "call flow",
        "data flow", "state flow", "module responsibilities", "dependency graph",
        "overall workflow", "global flow", "interaction", "how it works", "read order"
    };

    return text_contains_any(prompt, synthesis_keywords, sizeof(synthesis_keywords) / sizeof(synthesis_keywords[0])) ||
           text_contains_any(description, synthesis_keywords, sizeof(synthesis_keywords) / sizeof(synthesis_keywords[0]));
}
