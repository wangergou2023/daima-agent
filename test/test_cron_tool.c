#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "paths.h"
#include "kernel/time/timer.h"
#include "drivers/tool/tool_cron.h"

static void setup_paths(void)
{
    char home[128];
    char workdir[256];
    snprintf(home, sizeof(home), "/tmp/agent-cron-tool-home-%ld", (long)getpid());
    snprintf(workdir, sizeof(workdir), "%s/spiffs_data/workspace", home);
    mkdir(home, 0700);
    char spiffs[256];
    snprintf(spiffs, sizeof(spiffs), "%s/spiffs_data", home);
    mkdir(spiffs, 0700);
    mkdir(workdir, 0700);
    setenv("AGENT_HOME", home, 1);
    paths_init();
    cron_service_init();
}

int main(void)
{
    char out[4096];

    setup_paths();

    assert(tool_cron_execute("{\"action\":\"add\",\"name\":\"ping\",\"schedule_type\":\"every\",\"interval_s\":60,\"message\":\"hello\",\"channel\":\"system\"}", out, sizeof(out)) == 0);
    assert(strstr(out, "已添加周期任务"));

    assert(tool_cron_execute("{\"action\":\"list\"}", out, sizeof(out)) == 0);
    assert(strstr(out, "ping"));

    assert(tool_cron_execute("{\"action\":\"remove\"}", out, sizeof(out)) == ERR_INVALID_ARG);
    assert(strstr(out, "job_id"));

    assert(tool_cron_execute("{\"name\":\"ping\"}", out, sizeof(out)) == ERR_INVALID_ARG);
    assert(strstr(out, "action"));

    printf("cron tool tests passed\n");
    return 0;
}
