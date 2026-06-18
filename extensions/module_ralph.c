/* Ralph Loop 模块：回合结束时未完成 TODO 强制追加警告续推。在 on_finish 钩子中检查并追加中文续推提示。 */

#include "hooks.h"
#include "ralph.h"
#include "autoconf.h"
#include "linux/module.h"

#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"

/**
 * prepare 钩子：占位实现，当前无操作。
 */
static err_t on_prepare(struct message *msg, char *system_prompt,
                              size_t system_prompt_size, cJSON *messages)
{
    (void)system_prompt;
    (void)system_prompt_size;
    (void)messages;
    (void)msg;
    return 0;
}

/**
 * finish 钩子：占位实现，实际续推逻辑在 agent_extension_ralph_should_append_warning 中。
 */
static void on_finish(struct message *msg, const char *response)
{
    (void)msg;
    (void)response;
}

/**
 * 判断是否需要在响应末尾追加 Ralph Loop 续推警告。
 * 当回合输出中含 TODO 但未完成时，追加 "⚠️ 还有未完成的任务，请继续。"
 * @param msg           当前消息
 * @param iteration     当前迭代次数
 * @param io_final_text 输入/输出：最终文本（可能被 kmalloc 替换）
 * @return 已追加警告返回 true，否则返回 false
 */
bool agent_extension_ralph_should_append_warning(struct message *msg, int iteration, char **io_final_text)
{
#if AGENT_EXTENSIONS_ENABLED
    static const char warning[] = "\n\n⚠️ 还有未完成的任务，请继续。";
    if (!msg || !io_final_text) return false;
    const char *final_text = *io_final_text ? *io_final_text : "";
    if (!ralph_loop_should_continue(msg->chat_id, iteration, final_text)) return false;
    size_t final_len = strlen(final_text);
    size_t warning_len = sizeof(warning) - 1;
    char *with_warning = kmalloc(final_len + warning_len + 1, GFP_KERNEL);
    if (!with_warning) return false;
    memcpy(with_warning, final_text, final_len);
    memcpy(with_warning + final_len, warning, warning_len + 1);
    kfree(*io_final_text);
    *io_final_text = with_warning;
    return true;
#else
    (void)msg;
    (void)iteration;
    (void)io_final_text;
    return false;
#endif
}

static agent_extension_hooks_t ext = {
    .name = "ralph_loop",
    .on_prepare = on_prepare,
    .on_finish = on_finish,
    .enabled = true,
};

int __init ralph_module_init(void)
{
    agent_hooks_register(&ext);
    return 0;
}

module_init(ralph_module_init);
