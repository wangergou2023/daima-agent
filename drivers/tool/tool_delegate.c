/* delegate_task 工具：语义化子代理委托层。 */
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_delegate_route.h"
#include "linux/kernel.h"

static int s_delegate_seq = 0;

err_t delegate_launch_ready_background_subagents_for_runtime(void);

static struct tool s_delegate_task = {
    .name = "delegate_task",
    .description =
        "Delegate work to a semantic subagent."
        " Use this instead of doing broad discovery, architecture review, or large implementation yourself."
        " Never call this tool with an empty object."
        " For a fresh single subagent call, required fields are subagent_type + prompt."
        " Optional preflight_tool can force one concrete tool call before the child LLM turn starts."
        " For a fresh batch call, required field is tasks[] and every item must include subagent_type + description + prompt."
        " Batch calls may include team_name and dispatch_mode; dispatch_mode=staged enables queued children that wait for dependencies."
        " Each batch child may also include preflight_tool when a real protocol action must happen before analysis."
        " Each batch child may include task_key and depends_on so the coordinator can express a team-run task graph."
        " For continuation, provide task_id or coordinator_id."
        " Supported subagent_type values: explore, librarian, oracle, implement."
        " If you delegate work, do not duplicate the same exploration yourself in the same turn; wait for the subagent result and summarize from it."
        " Sync mode is the default path for focused delegated work; background mode is for long-running or parallel independent tasks."
        " For 2+ independent subtasks, prefer one batch call with tasks[] so they run under a coordinator."
        " For repo structure mapping, impact analysis, or pattern discovery, use subagent_type=explore before raw files traversal."
        " For architecture judgement, tradeoffs, or risk review, use subagent_type=oracle instead of answering directly."
        " For docs/config/reference lookup, use subagent_type=librarian."
        " For independent implementation shards, use subagent_type=implement and batch them only when target files do not overlap."
        " Use run_in_background=true to start a background task and receive a task_id."
        " Use task_id to resume or poll a previously started background subagent."
        " Use tasks[] to start multiple background subagents at once and receive a coordinator_id."
        " Use coordinator_id to poll a previously started delegated batch."
        " Use action=list to inspect the current parent session's delegated task registry."
        " When a coordinator batch reaches status=done, summarize directly from agents[].output; do not re-query every child task_id unless you are explicitly resuming one child session."
        " Rules: architecture questions must delegate to oracle; broad repo discovery must delegate to explore; documentation/reference lookup should delegate to librarian; implementation execution may delegate to implement."
        " Batch mode is preferred over multiple sibling delegate_task calls because the coordinator can manage parallel subagents as one unit.",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"minProperties\":1,"
        "\"properties\":{"
        "\"description\":{\"type\":\"string\",\"description\":\"Short task title\"},"
        "\"prompt\":{\"type\":\"string\",\"description\":\"Full delegated task prompt\"},"
        "\"preflight_tool\":{\"type\":\"object\",\"additionalProperties\":false,\"description\":\"Optional forced tool call executed before the delegated LLM turn begins.\",\"properties\":{\"tool_name\":{\"type\":\"string\",\"description\":\"Tool name to execute, for example terminal\"},\"input\":{\"type\":\"object\",\"description\":\"JSON tool input object for the forced tool call\"},\"continue_on_error\":{\"type\":\"boolean\",\"description\":\"Whether the child should continue even if the preflight tool blocks or fails\"}},\"required\":[\"tool_name\",\"input\"]},"
        "\"target_path\":{\"type\":\"string\",\"description\":\"Structured primary repo/file boundary for delegated exploration. For repo overview requests, set this to the repo root instead of relying only on prompt text.\"},"
        "\"subagent_type\":{\"type\":\"string\",\"enum\":[\"explore\",\"librarian\",\"oracle\",\"implement\"],\"description\":\"One of explore, librarian, oracle, implement\"},"
        "\"run_in_background\":{\"type\":\"boolean\",\"description\":\"true starts a background subagent and returns task_id\"},"
        "\"task_id\":{\"type\":\"string\",\"description\":\"Poll an existing background delegated task\"},"
        "\"coordinator_id\":{\"type\":\"string\",\"description\":\"Poll a background delegated coordinator batch\"},"
        "\"action\":{\"type\":\"string\",\"description\":\"Optional registry action. Supported: list\"},"
        "\"scope\":{\"type\":\"string\",\"description\":\"Optional action scope. Supported: parent\"},"
        "\"team_name\":{\"type\":\"string\",\"description\":\"Optional logical team name for a delegated batch/team-run\"},"
        "\"dispatch_mode\":{\"type\":\"string\",\"enum\":[\"parallel\",\"staged\"],\"description\":\"parallel launches all ready subtasks immediately; staged respects depends_on and may keep children queued until dependencies finish\"},"
        "\"tasks\":{\"type\":\"array\",\"minItems\":1,\"description\":\"Batch background delegated subtasks; each item must include subagent_type, description, prompt\",\"items\":{\"type\":\"object\",\"additionalProperties\":false,\"properties\":{\"task_key\":{\"type\":\"string\",\"description\":\"Stable child identifier within the batch for dependency references\"},\"description\":{\"type\":\"string\",\"description\":\"Short task title for this child subagent\"},\"prompt\":{\"type\":\"string\",\"description\":\"Full delegated task prompt for this child subagent\"},\"target_path\":{\"type\":\"string\",\"description\":\"Optional structured path boundary for this child subagent\"},\"depends_on\":{\"oneOf\":[{\"type\":\"string\"},{\"type\":\"array\",\"items\":{\"type\":\"string\"}}],\"description\":\"Optional upstream task_key or task_key list that must complete before this child starts\"},\"preflight_tool\":{\"type\":\"object\",\"additionalProperties\":false,\"description\":\"Optional forced tool call executed before this child LLM turn begins.\",\"properties\":{\"tool_name\":{\"type\":\"string\"},\"input\":{\"type\":\"object\"},\"continue_on_error\":{\"type\":\"boolean\"}},\"required\":[\"tool_name\",\"input\"]},\"subagent_type\":{\"type\":\"string\",\"enum\":[\"explore\",\"librarian\",\"oracle\",\"implement\"],\"description\":\"One of explore, librarian, oracle, implement for this child subagent\"}},\"required\":[\"description\",\"prompt\",\"subagent_type\"]}},"
        "\"load_skills\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Reserved for future skill injection\"},"
        "\"command\":{\"type\":\"string\",\"description\":\"Optional origin command/provenance field\"}"
        "},"
        "\"required\":[]}",
    .execute = tool_delegate_execute,
};

static int delegate_task_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_driver s_delegate_task_driver = {
    .drv.name = "delegate_task",
    .drv.probe = delegate_task_tool_probe,
    .execute = tool_delegate_execute,
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

int tool_delegate_next_seq(void)
{
    return ++s_delegate_seq;
}

int tool_delegate_current_seq(void)
{
    return s_delegate_seq;
}
