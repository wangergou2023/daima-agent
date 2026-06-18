#include <assert.h>
#include <string.h>

#include "roles.h"

static void assert_role_sequence(enum intent intent,
                                 const agent_role_t *expected,
                                 int expected_count)
{
    agent_role_t roles[3] = {AGENT_ROLE_COUNT, AGENT_ROLE_COUNT, AGENT_ROLE_COUNT};
    int actual_count = agent_roles_for_intent(intent, roles);

    assert(actual_count == expected_count);
    for (int i = 0; i < expected_count; i++) {
        assert(roles[i] == expected[i]);
    }
}

int main(void)
{
    const agent_role_t qa_roles[] = {AGENT_ROLE_FAST};
    const agent_role_t open_roles[] = {AGENT_ROLE_FAST};
    const agent_role_t investigate_roles[] = {AGENT_ROLE_FAST};
    const agent_role_t implement_roles[] = {
        AGENT_ROLE_PLANNER,
        AGENT_ROLE_EXECUTOR,
        AGENT_ROLE_REVIEWER,
    };
    const agent_role_t fix_roles[] = {
        AGENT_ROLE_PLANNER,
        AGENT_ROLE_EXECUTOR,
    };

    assert_role_sequence(INTENT_QA, qa_roles, 1);
    assert_role_sequence(INTENT_OPEN, open_roles, 1);
    assert_role_sequence(INTENT_INVESTIGATE, investigate_roles, 1);
    assert_role_sequence(INTENT_IMPLEMENT, implement_roles, 3);
    assert_role_sequence(INTENT_FIX, fix_roles, 2);

    for (int i = 0; i < AGENT_ROLE_COUNT; i++) {
        agent_role_t role = (agent_role_t)i;
        assert(agent_role_name(role) != NULL);
        assert(agent_role_name(role)[0] != '\0');
        assert(agent_role_prompt_suffix(role) != NULL);
        assert(agent_role_prompt_suffix(role)[0] != '\0');
        assert(agent_role_category(role) != NULL);
        assert(agent_role_category(role)[0] != '\0');
    }

    assert(strcmp(agent_role_name(AGENT_ROLE_FAST), "FAST") == 0);
    assert(strcmp(agent_role_name(AGENT_ROLE_PLANNER), "PLANNER") == 0);
    assert(strcmp(agent_role_name(AGENT_ROLE_EXECUTOR), "EXECUTOR") == 0);
    assert(strcmp(agent_role_name(AGENT_ROLE_REVIEWER), "REVIEWER") == 0);

    assert(agent_roles_for_intent(INTENT_COUNT, NULL) == 0);

    return 0;
}
