#include <assert.h>
#include <stdio.h>

#include "drivers/tool/tool_delegate_batch_policy.h"

int main(void)
{
    assert(tool_delegate_prompt_prefers_unified_repo_analysis(
        "分析 /home/wangergou/code/github/oh-my-pi 仓库的代码架构、主流程和模块职责",
        "分析 oh-my-pi 代码架构"));

    assert(tool_delegate_prompt_prefers_unified_repo_analysis(
        "Explain the repository architecture, main flow, and module responsibilities",
        ""));

    assert(!tool_delegate_prompt_prefers_unified_repo_analysis(
        "列出 /home/wangergou/code/github/oh-my-pi/scripts 和 /home/wangergou/code/github/oh-my-pi/python 的文件",
        "比较两个目录"));

    assert(!tool_delegate_prompt_prefers_unified_repo_analysis(
        "只看 /home/wangergou/code/github/oh-my-pi/scripts/install.sh 这个文件",
        "read one file"));

    puts("PASS");
    return 0;
}
