#include "drivers/tool/tool_delegate_prepare.h"

#include <string.h>

#include "autoconf.h"
#include "drivers/tool/tool_delegate_overview.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include "linux/kernel.h"

void tool_delegate_normalize_batch_child_request(delegate_request_t *child)
{
    char prepared_prompt[READ_FILE_MAX_CHARS + 4096];
    bool disable_tools = false;

    if (!child) {
        return;
    }

    if (strcmp(child->subagent_type, "explore") == 0 && !child->target_path[0]) {
        char resolved_path[512];
        if (tool_delegate_extract_single_absolute_repo_path(child->prompt,
                                                            resolved_path,
                                                            sizeof(resolved_path))) {
            strscpy(child->target_path, resolved_path, sizeof(child->target_path));
        }
    }

    if (!tool_delegate_prepare_subagent_prompt(child->subagent_type,
                                               child->description,
                                               child->target_path,
                                               child->prompt,
                                               prepared_prompt,
                                               sizeof(prepared_prompt),
                                               &disable_tools)) {
        return;
    }

    if (prepared_prompt[0]) {
        strscpy(child->prompt, prepared_prompt, sizeof(child->prompt));
    }
}
