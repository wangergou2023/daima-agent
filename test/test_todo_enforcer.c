#include "todo.h"
#include "paths.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void ensure_dir(const char *path)
{
    if (mkdir(path, 0700) != 0) {
    }
}

static void setup_home(void)
{
    const char *home = "/tmp/agent-todo-enforcer-test";
    setenv("AGENT_HOME", home, 1);
    ensure_dir(home);
    paths_init();
    ensure_dir(path_session_dir());
    todo_enforcer_reset("chat-a");
}

static void test_stale_turns_inject_warning_after_threshold(void)
{
    setup_home();

    assert(todo_enforcer_record_progress("chat-a", 3, 0) == 0);
    assert(todo_enforcer_record_progress("chat-a", 3, 0) == 0);
    assert(todo_enforcer_record_progress("chat-a", 3, 0) == 0);

    char prompt[2048] = "base prompt";
    assert(todo_enforcer_inject_prompt("chat-a", prompt, sizeof(prompt)) == 0);
    assert(strstr(prompt, "任务进度警告") != NULL);
    assert(strstr(prompt, "连续 3 轮") != NULL);
    assert(strstr(prompt, "当前待办: 3个, 已完成: 0个") != NULL);
}

static void test_completion_progress_resets_stale_counter(void)
{
    setup_home();

    assert(todo_enforcer_record_progress("chat-a", 2, 0) == 0);
    assert(todo_enforcer_record_progress("chat-a", 2, 0) == 0);
    assert(todo_enforcer_record_progress("chat-a", 2, 1) == 0);

    char prompt[1024] = "base prompt";
    assert(todo_enforcer_inject_prompt("chat-a", prompt, sizeof(prompt)) == 0);
    assert(strstr(prompt, "任务进度警告") == NULL);
}

static void test_reset_removes_enforcer_state(void)
{
    setup_home();

    assert(todo_enforcer_record_progress("chat-a", 1, 0) == 0);
    assert(todo_enforcer_record_progress("chat-a", 1, 0) == 0);
    assert(todo_enforcer_record_progress("chat-a", 1, 0) == 0);
    assert(todo_enforcer_reset("chat-a") == 0);

    char prompt[1024] = "base prompt";
    assert(todo_enforcer_inject_prompt("chat-a", prompt, sizeof(prompt)) == 0);
    assert(strcmp(prompt, "base prompt") == 0);
}

int main(void)
{
    test_stale_turns_inject_warning_after_threshold();
    test_completion_progress_resets_stale_counter();
    test_reset_removes_enforcer_state();
    puts("test_todo_enforcer passed");
    return 0;
}
