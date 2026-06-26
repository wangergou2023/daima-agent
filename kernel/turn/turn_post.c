/* Turn 收尾副作用：cleanup / recovery / todo / compaction / tool cleanup。 */

#include "turn_post.h"

#include <stdio.h>

#include "autoconf.h"
#include "compaction.h"
#include "paths.h"
#include "recovery.h"
#include "todo.h"
#include "turn_common.h"
#include "turn_dispatch.h"
#include "cjson.h"
#include "drivers/platform/platform.h"
#include "linux/kernel.h"
#include "os.h"
#if SKILL_SCOPED_TOOLS_ENABLED
#include "drivers/skill/skill_tools.h"
#include "linux/slab.h"
#endif

#ifdef TODO_ENFORCER_ENABLED
static void read_todo_counts(int *out_total, int *out_completed)
{
	*out_total = 0;
	*out_completed = 0;
	char todo_path[512];
	snprintf(todo_path, sizeof(todo_path), "%s/TODO.json", path_memory_dir());
	FILE *f = fopen(todo_path, "r");
	if (!f) {
		return;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < 0 || size > 128 * 1024) {
		fclose(f);
		return;
	}

	char *buf = kzalloc((size_t)size + 1, GFP_KERNEL);
	if (!buf) {
		fclose(f);
		return;
	}

	size_t n = fread(buf, 1, (size_t)size, f);
	fclose(f);
	buf[n] = '\0';

	cJSON *root = cJSON_Parse(buf);
	kfree(buf);
	if (!root || !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return;
	}

	cJSON *items = cJSON_GetObjectItem(root, "items");
	if (items && cJSON_IsArray(items)) {
		cJSON *item = NULL;
		cJSON_ArrayForEach(item, items) {
			(*out_total)++;
			cJSON *done = cJSON_GetObjectItem(item, "done");
			if (cJSON_IsTrue(done) || (cJSON_IsNumber(done) && done->valueint != 0)) {
				(*out_completed)++;
			}
		}
	}
	cJSON_Delete(root);
}
#endif

void agent_turn_run_post_actions(struct message *msg,
				 err_t turn_err,
				 bool cancelled)
{
	agent_cleanup_inbound_msg(msg);

#if SKILL_SCOPED_TOOLS_ENABLED
	skill_tools_unregister_all();
#endif

	if (cancelled) {
		return;
	}

	if (IS_ENABLED(CONFIG_COMPACTION_RECOVERY_ENABLED) &&
	    turn_err == 0 && msg && msg->chat_id[0]) {
		compaction_recovery_clear(msg->chat_id);
	}
	if (IS_ENABLED(CONFIG_TODO_ENFORCER_ENABLED) &&
	    turn_err == 0 && msg && msg->chat_id[0]) {
		int total_todos = 0;
		int completed_todos = 0;
		read_todo_counts(&total_todos, &completed_todos);
		todo_enforcer_record_progress(msg->chat_id, total_todos, completed_todos);
	}
	if (IS_ENABLED(CONFIG_SESSION_RECOVERY_ENABLED) &&
	    turn_err == 0 && msg && msg->chat_id[0]) {
		session_recovery_clear(msg->chat_id);
	}
	if (turn_err == 0 && msg && msg->chat_id[0]) {
		dispatch_compress_context(msg->chat_id);
	}

	char free_mem_buf[32];
	if (platform_format_bytes(platform_free_memory(), free_mem_buf, sizeof(free_mem_buf))) {
		pr_info("Free memory: %s bytes", free_mem_buf);
	} else {
		pr_info("Free memory: <unavailable> bytes");
	}
}
