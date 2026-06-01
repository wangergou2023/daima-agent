/* 会话存储抽象层调度。 */

#include "session_store.h"

#include "daima_log.h"

static const char *TAG = "session_store";
static const daima_session_store_ops_t *s_ops = NULL;

static daima_err_t ensure_store_ready(void)
{
    if (s_ops) {
        return DAIMA_OK;
    }
    return session_store_init();
}

daima_err_t session_store_init(void)
{
    s_ops = session_store_file_backend();
    if (!s_ops) {
        DAIMA_LOGE(TAG, "No session store backend available");
        return DAIMA_FAIL;
    }
    if (!s_ops->init) {
        DAIMA_LOGE(TAG, "Session store backend missing init");
        return DAIMA_ERR_INVALID_ARG;
    }
    daima_err_t err = s_ops->init();
    if (err == DAIMA_OK) {
        DAIMA_LOGI(TAG, "Session store backend ready");
    }
    return err;
}

daima_err_t session_store_append(const char *chat_id, const char *role, const char *content)
{
    return session_store_append_ex(chat_id, role, content, NULL);
}

daima_err_t session_store_append_ex(const char *chat_id,
                                   const char *role,
                                   const char *content,
                                   const char *source)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->append_ex ? s_ops->append_ex(chat_id, role, content, source) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->get_history_json ? s_ops->get_history_json(chat_id, buf, size, max_msgs) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_rewrite_from_array(const char *chat_id, const cJSON *messages)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->rewrite_from_array ? s_ops->rewrite_from_array(chat_id, messages) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_read_facts(const char *chat_id, char *buf, size_t size)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->read_facts ? s_ops->read_facts(chat_id, buf, size) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_merge_facts(const char *chat_id, const char *facts_text)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->merge_facts ? s_ops->merge_facts(chat_id, facts_text) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_read_summary(const char *chat_id, char *buf, size_t size)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->read_summary ? s_ops->read_summary(chat_id, buf, size) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_write_summary(const char *chat_id, const char *summary_text)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->write_summary ? s_ops->write_summary(chat_id, summary_text) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_clear(const char *chat_id)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->clear ? s_ops->clear(chat_id) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_list_records(daima_session_record_t *records, size_t capacity, int *out_count)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->list_records ? s_ops->list_records(records, capacity, out_count) : DAIMA_ERR_INVALID_ARG;
}

daima_err_t session_store_artifact_path(const char *chat_id,
                                       daima_session_artifact_kind_t kind,
                                       char *buf,
                                       size_t size)
{
    daima_err_t err = ensure_store_ready();
    if (err != DAIMA_OK) return err;
    return s_ops->artifact_path ? s_ops->artifact_path(chat_id, kind, buf, size) : DAIMA_ERR_INVALID_ARG;
}
