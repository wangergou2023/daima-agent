#include "ralph.h"
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
    const char *home = "/tmp/agent-ralph-loop-test";
    char spiffs[256];
    setenv("AGENT_HOME", home, 1);
    ensure_dir(home);
    snprintf(spiffs, sizeof(spiffs), "%s/spiffs_data", home);
    ensure_dir(spiffs);
    paths_init();
    ensure_dir(path_session_dir());
    ralph_loop_reset("chat-a");
}

static void write_session_todo(const char *chat_id, const char *json)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/session_%s_TODO.json", path_session_dir(), chat_id);
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fwrite(json, 1, strlen(json), f) == strlen(json));
    fclose(f);
}

static void test_continues_when_active_todo_and_iteration_under_limit(void)
{
    setup_home();
    write_session_todo("chat-a", "{\"items\":[{\"id\":1,\"text\":\"do work\",\"done\":false}]}");

    assert(ralph_loop_should_continue("chat-a", 0, "done for now") == true);
}

static void test_stops_when_all_todos_completed(void)
{
    setup_home();
    write_session_todo("chat-a", "{\"items\":[{\"id\":1,\"text\":\"do work\",\"done\":true}]}");

    assert(ralph_loop_should_continue("chat-a", 0, "finished") == false);
}

static void test_stops_at_max_iterations(void)
{
    setup_home();
    write_session_todo("chat-a", "{\"items\":[{\"id\":1,\"text\":\"do work\",\"done\":false}]}");

    assert(ralph_loop_should_continue("chat-a", RALPH_LOOP_MAX_ITERATIONS, "still working") == false);
}

static void test_missing_todo_file_does_not_continue(void)
{
    setup_home();

    assert(ralph_loop_should_continue("chat-a", 0, "nothing") == false);
}

int main(void)
{
    test_continues_when_active_todo_and_iteration_under_limit();
    test_stops_when_all_todos_completed();
    test_stops_at_max_iterations();
    test_missing_todo_file_does_not_continue();
    puts("test_ralph_loop passed");
    return 0;
}
