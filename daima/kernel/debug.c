#include "debug.h"
#include "fs.h"
#include "paths.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "turn_common.h"
#include "autoconf.h"

void agent_prompt_dump_snapshot(const struct message *msg, const char *system_prompt)
{
    if (!msg || !system_prompt || !system_prompt[0]) {
        return;
    }
    if (!agent_env_bool_or_default("DEBUG_PROMPT_DUMP", DEBUG_PROMPT_DUMP != 0)) {
        return;
    }

    daima_fs_ensure_dir(daima_path_cache_dir());

    char chat_slug[96];
    agent_chat_id_to_slug(msg->chat_id, chat_slug, sizeof(chat_slug));

    char per_chat_path[256];
    snprintf(per_chat_path, sizeof(per_chat_path), "%s/prompt_%s.md", daima_path_cache_dir(), chat_slug);

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char ts_buf[64];
    strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M:%S %Z", &tm_info);

    char paths_block[BUF_LARGE];
    snprintf(paths_block,
             sizeof(paths_block),
             "## 相关路径\n\n"
             "- `BOOTSTRAP.md`: `%s`\n"
             "- `IDENTITY.md`: `%s`\n"
             "- `SOUL.md`: `%s`\n"
             "- `USER.md`: `%s`\n"
             "- `MEMORY.md`: `%s`\n"
             "- `Skills`: `%s`\n",
             daima_path_bootstrap_file(),
             daima_path_identity_file(),
             daima_path_soul_file(),
             daima_path_user_file(),
             daima_path_memory_file(),
             daima_path_skills_dir());

    const char *meta_fmt =
        "# Final Prompt Snapshot\n\n"
        "## Metadata\n\n"
        "- Time: %s\n"
        "- Channel: %s\n"
        "- Chat ID: %s\n"
        "- Source: %s\n\n"
        "%s\n"
        "## Prompt\n\n"
        "%s\n";

    FILE *f = fopen(daima_path_prompt_debug_file(), "w");
    if (f) {
        fprintf(f, meta_fmt,
                ts_buf,
                msg->channel[0] ? msg->channel : "(unknown)",
                msg->chat_id[0] ? msg->chat_id : "(empty)",
                agent_msg_source_or_default(msg),
                paths_block,
                system_prompt);
        fclose(f);
    }

    f = fopen(per_chat_path, "w");
    if (f) {
        fprintf(f, meta_fmt,
                ts_buf,
                msg->channel[0] ? msg->channel : "(unknown)",
                msg->chat_id[0] ? msg->chat_id : "(empty)",
                agent_msg_source_or_default(msg),
                paths_block,
                system_prompt);
        fclose(f);
    }
}
