/* 会话文件存储——内部接口。
 * 定义文件级存储的实现函数，包括路径生成、事实存取、摘要读写。 */

#pragma once

#include <stddef.h>

#include "autoconf.h"
#include "drivers/memory/session_store.h"

/* 生成指定品类（消息/事实/摘要等）的文件路径 */
err_t session_store_file_artifact_path(const char *chat_id,
                                             session_artifact_kind_t kind,
                                             char *buf,
                                             size_t size);

/* 事实文件读写 */
err_t session_store_file_read_facts(const char *chat_id, char *buf, size_t size);
err_t session_store_file_merge_facts(const char *chat_id, const char *facts_text);

/* 摘要文件读写 */
err_t session_store_file_read_summary(const char *chat_id, char *buf, size_t size);
err_t session_store_file_write_summary(const char *chat_id, const char *summary_text);
