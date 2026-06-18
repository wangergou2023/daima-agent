#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compaction.h"
#include "paths.h"
#include "drivers/memory/session_store.h"

static void make_dir(const char *path)
{
    assert(mkdir(path, 0700) == 0 || access(path, F_OK) == 0);
}

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    fclose(f);
}

static void recovery_path(const char *chat_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/session_%s_recovery.json", path_session_dir(), chat_id);
}

int main(void)
{
    const char *home = "/tmp/compaction_recovery_test";
    setenv("AGENT_HOME", home, 1);
    make_dir(home);

    char spiffs[1024];
    assert(strlen(home) + strlen("/spiffs_data") + 1 < sizeof(spiffs));
    strcpy(spiffs, home);
    strcat(spiffs, "/spiffs_data");
    make_dir(spiffs);

    char sessions[1024];
    assert(strlen(spiffs) + strlen("/sessions") + 1 < sizeof(sessions));
    strcpy(sessions, spiffs);
    strcat(sessions, "/sessions");
    make_dir(sessions);

    paths_init();
    assert(session_store_init() == 0);

    const char *chat_id = "recovery_case";
    char facts_path[256];
    char summary_path[256];
    assert(session_store_artifact_path(chat_id, SESSION_ARTIFACT_FACTS, facts_path, sizeof(facts_path)) == 0);
    assert(session_store_artifact_path(chat_id, SESSION_ARTIFACT_SUMMARY, summary_path, sizeof(summary_path)) == 0);

    write_text(facts_path,
               "## 会话事实卡片\n"
               "- [ ] 实现 snapshot\n"
               "不是 todo\n"
               "- [x] 保留已完成项用于恢复\n");
    write_text(summary_path,
               "## 最近一次上下文压缩摘要\n"
               "更新时间：2026-06-12\n\n"
               "当前任务：实现 T5 CompactionRecovery。\n"
               "用户最近要求恢复 todo 和任务。\n");

    assert(compaction_recovery_snapshot(chat_id) == 0);

    char prompt[2048] = "基础系统提示\n";
    assert(compaction_recovery_inject(chat_id, prompt, sizeof(prompt)) == 0);
    assert(strstr(prompt, "## 上下文恢复") != NULL);
    assert(strstr(prompt, "- 活跃待办: - [ ] 实现 snapshot") != NULL);
    assert(strstr(prompt, "- 当前任务: 当前任务：实现 T5 CompactionRecovery。") != NULL);
    assert(strstr(prompt, "请基于以上信息继续之前的工作。") != NULL);

    assert(compaction_recovery_clear(chat_id) == 0);
    char recovery[256];
    recovery_path(chat_id, recovery, sizeof(recovery));
    assert(access(recovery, F_OK) != 0);

    return 0;
}
