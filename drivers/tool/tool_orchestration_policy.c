#include "drivers/tool/tool_orchestration_policy.h"

#include "drivers/tool/tool_decomposition_policy.h"

bool tool_orchestration_policy_requires_delegate_only(const struct message *msg)
{
    return tool_decomposition_policy_requires_delegate_only(msg);
}
