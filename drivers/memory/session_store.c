/* 会话存储抽象层调度。 */

#include "session_store.h"

#include "linux/printk.h"
static const session_store_ops_t *s_ops = NULL;

static err_t ensure_store_ready(void)
{
    if (s_ops) {
        return 0;
    }
    return session_store_init();
}

err_t session_store_init(void)
{
    s_ops = session_store_file_backend();
    if (!s_ops) {
        pr_err("No session store backend available");
        return ERR_FAIL;
    }
    if (!s_ops->init) {
        pr_err("Session store backend missing init");
        return ERR_INVALID_ARG;
    }
    err_t err = s_ops->init();
    if (err == 0) {
        pr_info("Session store backend ready");
    }
    return err;
}

err_t session_store_append(const char *chat_id, const char *role, const char *content)
{
    return session_store_append_ex(chat_id, role, content, NULL);
}

err_t session_store_append_ex(const char *chat_id,
                                   const char *role,
                                   const char *content,
                                   const char *source)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->append_ex ? s_ops->append_ex(chat_id, role, content, source) : ERR_INVALID_ARG;
}

err_t session_store_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->get_history_json ? s_ops->get_history_json(chat_id, buf, size, max_msgs) : ERR_INVALID_ARG;
}

err_t session_store_get_history_window_meta(const char *chat_id,
                                            int max_msgs,
                                            session_history_window_meta_t *meta)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->get_history_window_meta ? s_ops->get_history_window_meta(chat_id, max_msgs, meta)
                                          : ERR_INVALID_ARG;
}

err_t session_store_rewrite_from_array(const char *chat_id, const cJSON *messages)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->rewrite_from_array ? s_ops->rewrite_from_array(chat_id, messages) : ERR_INVALID_ARG;
}

err_t session_store_read_facts(const char *chat_id, char *buf, size_t size)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->read_facts ? s_ops->read_facts(chat_id, buf, size) : ERR_INVALID_ARG;
}

err_t session_store_merge_facts(const char *chat_id, const char *facts_text)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->merge_facts ? s_ops->merge_facts(chat_id, facts_text) : ERR_INVALID_ARG;
}

err_t session_store_read_summary(const char *chat_id, char *buf, size_t size)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->read_summary ? s_ops->read_summary(chat_id, buf, size) : ERR_INVALID_ARG;
}

err_t session_store_write_summary(const char *chat_id, const char *summary_text)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->write_summary ? s_ops->write_summary(chat_id, summary_text) : ERR_INVALID_ARG;
}

err_t session_store_clear(const char *chat_id)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->clear ? s_ops->clear(chat_id) : ERR_INVALID_ARG;
}

err_t session_store_list_records(session_record_t *records, size_t capacity, int *out_count)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->list_records ? s_ops->list_records(records, capacity, out_count) : ERR_INVALID_ARG;
}

err_t session_store_artifact_path(const char *chat_id,
                                       session_artifact_kind_t kind,
                                       char *buf,
                                       size_t size)
{
    err_t err = ensure_store_ready();
    if (err != 0) return err;
    return s_ops->artifact_path ? s_ops->artifact_path(chat_id, kind, buf, size) : ERR_INVALID_ARG;
}
