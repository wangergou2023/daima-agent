/* MCP JSON-RPC 2.0 客户端实现
 * 通过 fork+exec+pipe 启动 robot-mcp 子进程，双向通信。
 */
#include "drivers/channel/vector/mcp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>

#include "cJSON.h"
#include "linux/printk.h"
#include "autoconf.h"

static bool handle_mcp_notification(mcp_client_t *c, const char *json_str);
static daima_err_t read_json_line(mcp_client_t *c, char *buf, size_t size, int timeout_ms);
static daima_err_t wait_for_response(mcp_client_t *c, int request_id, int timeout_ms,
                                     char *response, size_t response_size);

static const char *TAG = "mcp_client";

struct mcp_client {
    FILE   *in;            /* 子进程 stdin  (写入) */
    FILE   *out;           /* 子进程 stdout (读取) */
    pid_t   pid;
    int     request_id;
    pthread_mutex_t io_mutex;
    char    buf[MCP_READ_BUF_SIZE];
    char    pending[MCP_READ_BUF_SIZE];
    mcp_audio_callback_t audio_cb;
    void   *audio_user_data;
    mcp_audio_done_callback_t audio_done_cb;
    void   *audio_done_user_data;
};

mcp_client_t *mcp_client_launch(const char *bin_path, const char *robot_addr, const char *token_file)
{
    if (!bin_path || !bin_path[0]) return NULL;

    const char *addr = robot_addr && robot_addr[0] ? robot_addr : "localhost:443";
    const char *token = token_file && token_file[0] ? token_file : "/run/vic-cloud/perRuntimeToken";

    int to_child[2], from_child[2];
    if (pipe(to_child) < 0 || pipe(from_child) < 0) {
        DAIMA_LOGE(TAG, "pipe failed: %s", strerror(errno));
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        DAIMA_LOGE(TAG, "fork failed: %s", strerror(errno));
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child: set up pipes and exec robot-mcp */
        close(to_child[1]);   /* close write end of stdin pipe */
        close(from_child[0]); /* close read end of stdout pipe */
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]);
        close(from_child[1]);

        /* Environment variables */
        char addr_env[512], token_env[512];
        snprintf(addr_env, sizeof(addr_env), "ROBOT_ADDR=%s", addr);
        snprintf(token_env, sizeof(token_env), "TOKEN_FILE=%s", token);
        char *envp[] = { addr_env, token_env, "INSECURE=1", NULL };

        /* Redirect stderr to parent's stderr so we see robot-mcp logs */
        execve(bin_path, (char *[]){ (char *)bin_path, (char *)"--client-only", NULL }, envp);

        DAIMA_LOGE(TAG, "execve failed: %s", strerror(errno));
        _exit(1);
    }

    /* Parent */
    close(to_child[0]);   /* close read end of stdin pipe */
    close(from_child[1]); /* close write end of stdout pipe */

    mcp_client_t *c = calloc(1, sizeof(mcp_client_t));
    if (!c) {
        close(to_child[1]);
        close(from_child[0]);
        return NULL;
    }
    c->in  = fdopen(to_child[1], "w");
    c->out = fdopen(from_child[0], "r");
    c->pid = pid;
    c->request_id = 0;
    pthread_mutex_init(&c->io_mutex, NULL);
    if (!c->in || !c->out) {
        DAIMA_LOGE(TAG, "fdopen failed");
        mcp_client_destroy(c);
        return NULL;
    }

    /* Set line buffering so MCP JSON messages are sent/received immediately */
    setlinebuf(c->in);

    DAIMA_LOGI(TAG, "Launched robot-mcp (pid=%d)", (int)pid);

    /* MCP 握手: initialize */
    cJSON *init_params = cJSON_CreateObject();
    cJSON_AddStringToObject(init_params, "protocolVersion", "2025-03-26");
    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(init_params, "capabilities", capabilities);
    cJSON *client_info = cJSON_CreateObject();
    cJSON_AddStringToObject(client_info, "name", "daima-agent");
    cJSON_AddStringToObject(client_info, "version", "1.0.0");
    cJSON_AddItemToObject(init_params, "clientInfo", client_info);

    cJSON *init_req = cJSON_CreateObject();
    int init_id = ++c->request_id;
    cJSON_AddStringToObject(init_req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(init_req, "id", (double)init_id);
    cJSON_AddStringToObject(init_req, "method", "initialize");
    cJSON_AddItemToObject(init_req, "params", init_params);

    char *init_str = cJSON_PrintUnformatted(init_req);
    cJSON_Delete(init_req);

    if (!init_str || fprintf(c->in, "%s\n", init_str) < 0 || fflush(c->in) != 0) {
        free(init_str);
        goto fail;
    }
    free(init_str);

    if (wait_for_response(c, init_id, MCP_INIT_TIMEOUT_MS, c->buf, sizeof(c->buf)) != DAIMA_OK) {
        DAIMA_LOGE(TAG, "No response to initialize");
        goto fail;
    }
    DAIMA_LOGI(TAG, "Initialize response: %.120s", c->buf);

    cJSON *init_resp = cJSON_Parse(c->buf);
    if (!init_resp) goto fail;

    cJSON *result = cJSON_GetObjectItem(init_resp, "result");
    if (!result) {
        DAIMA_LOGE(TAG, "Initialize failed: no result");
        cJSON_Delete(init_resp);
        goto fail;
    }
    cJSON_Delete(init_resp);

    /* 发送 initialized 通知 */
    fprintf(c->in, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
    fflush(c->in);

    DAIMA_LOGI(TAG, "MCP client ready (id=%d)", c->request_id);
    return c;

fail:
    mcp_client_destroy(c);
    return NULL;
}

void mcp_client_destroy(mcp_client_t *c)
{
    if (!c) return;
    pthread_mutex_lock(&c->io_mutex);
    if (c->in) {
        /* Tell robot-mcp to unsubscribe and stop */
        fprintf(c->in, "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"drivers/tool/call\",\"params\":{\"name\":\"robot_unsubscribe_audio\",\"arguments\":{}}}\n");
        fflush(c->in);
        fclose(c->in);
        c->in = NULL;
    }
    if (c->out) {
        fclose(c->out);
        c->out = NULL;
    }
    pthread_mutex_unlock(&c->io_mutex);
    if (c->pid > 0) {
        /* Give child a moment to exit gracefully, then kill */
        int status;
        kill(c->pid, SIGTERM);
        waitpid(c->pid, &status, 0);
    }
    pthread_mutex_destroy(&c->io_mutex);
    free(c);
}

daima_err_t mcp_client_call_tool(mcp_client_t *c, const char *tool_name,
                                const char *args_json,
                                char *response_out, size_t response_size)
{
    if (!c || !c->in || !c->out || !tool_name || !response_out || response_size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    /* 构造请求参数 (lock-free, local operations) */
    cJSON *params = cJSON_CreateObject();
    if (!params) return DAIMA_ERR_NO_MEM;
    cJSON_AddStringToObject(params, "name", tool_name);
    cJSON *args = cJSON_Parse(args_json ? args_json : "{}");
    if (!args) args = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "arguments", args);

    pthread_mutex_lock(&c->io_mutex);
    if (!c->in || !c->out) {
        pthread_mutex_unlock(&c->io_mutex);
        cJSON_Delete(params);
        return DAIMA_ERR_INVALID_ARG;
    }

    cJSON *req = cJSON_CreateObject();
    if (!req) {
        pthread_mutex_unlock(&c->io_mutex);
        cJSON_Delete(params);
        return DAIMA_ERR_NO_MEM;
    }
    int request_id = ++c->request_id;
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", (double)request_id);
    cJSON_AddStringToObject(req, "method", "drivers/tool/call");
    cJSON_AddItemToObject(req, "params", params);

    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    if (!req_str || fprintf(c->in, "%s\n", req_str) < 0 || fflush(c->in) != 0) {
        free(req_str);
        pthread_mutex_unlock(&c->io_mutex);
        snprintf(response_out, response_size, "Error: write failed");
        return DAIMA_FAIL;
    }
    free(req_str);

    response_out[0] = '\0';

    daima_err_t wait_err = wait_for_response(c, request_id, MCP_CALL_TIMEOUT_MS,
                                             c->buf, sizeof(c->buf));
    pthread_mutex_unlock(&c->io_mutex);
    if (wait_err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "No response to tools/call %s (id=%d)", tool_name, request_id);
        snprintf(response_out, response_size, "Error: no response");
        return wait_err;
    }

    DAIMA_LOGI(TAG, "RAW response for %s: %s", tool_name, c->buf);

    cJSON *resp = cJSON_Parse(c->buf);
    if (!resp) {
        snprintf(response_out, response_size, "Error: invalid JSON response");
        return DAIMA_FAIL;
    }

    /* 提取 result.content[0].text */
    cJSON *result = cJSON_GetObjectItem(resp, "result");
    if (result) {
        cJSON *content = cJSON_GetObjectItem(result, "content");
        if (content && cJSON_IsArray(content)) {
            cJSON *first = cJSON_GetArrayItem(content, 0);
            if (first) {
                cJSON *text = cJSON_GetObjectItem(first, "text");
                if (text && cJSON_IsString(text)) {
                    snprintf(response_out, response_size, "%s", text->valuestring);
                }
            }
        }
    } else {
        cJSON *err = cJSON_GetObjectItem(resp, "error");
        if (err) {
            cJSON *msg = cJSON_GetObjectItem(err, "message");
            snprintf(response_out, response_size, "Error: %s",
                     msg && cJSON_IsString(msg) ? msg->valuestring : "unknown");
        }
    }

    cJSON_Delete(resp);
    return DAIMA_OK;
}

daima_err_t mcp_client_list_tools(mcp_client_t *c, char *tools_json_out, size_t tools_size)
{
    if (!c || !tools_json_out || tools_size == 0) return DAIMA_ERR_INVALID_ARG;

    pthread_mutex_lock(&c->io_mutex);
    if (!c->in || !c->out) {
        pthread_mutex_unlock(&c->io_mutex);
        tools_json_out[0] = '\0';
        return DAIMA_ERR_INVALID_ARG;
    }

    cJSON *req = cJSON_CreateObject();
    if (!req) {
        pthread_mutex_unlock(&c->io_mutex);
        tools_json_out[0] = '\0';
        return DAIMA_ERR_NO_MEM;
    }
    int request_id = ++c->request_id;
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", (double)request_id);
    cJSON_AddStringToObject(req, "method", "drivers/tool/list");

    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    if (!req_str || fprintf(c->in, "%s\n", req_str) < 0 || fflush(c->in) != 0) {
        free(req_str);
        pthread_mutex_unlock(&c->io_mutex);
        tools_json_out[0] = '\0';
        return DAIMA_FAIL;
    }
    free(req_str);

    daima_err_t wait_err = wait_for_response(c, request_id, MCP_CALL_TIMEOUT_MS,
                                             c->buf, sizeof(c->buf));
    pthread_mutex_unlock(&c->io_mutex);
    if (wait_err != DAIMA_OK) {
        tools_json_out[0] = '\0';
        return wait_err;
    }

    cJSON *resp = cJSON_Parse(c->buf);
    if (!resp) {
        tools_json_out[0] = '\0';
        return DAIMA_FAIL;
    }
    cJSON *result = cJSON_GetObjectItem(resp, "result");
    if (result) {
        cJSON *tools = cJSON_GetObjectItem(result, "tools");
        if (tools) {
            char *s = cJSON_PrintUnformatted(tools);
            snprintf(tools_json_out, tools_size, "%s", s ? s : "[]");
            free(s);
        } else {
            snprintf(tools_json_out, tools_size, "[]");
        }
    }
    cJSON_Delete(resp);
    return DAIMA_OK;
}

daima_err_t mcp_client_subscribe_audio(mcp_client_t *c)
{
    char resp[DAIMA_BUF_SMALL];
    return mcp_client_call_tool(c, "robot_subscribe_audio", "{}", resp, sizeof(resp));
}

daima_err_t mcp_client_unsubscribe_audio(mcp_client_t *c)
{
    char resp[DAIMA_BUF_SMALL];
    return mcp_client_call_tool(c, "robot_unsubscribe_audio", "{}", resp, sizeof(resp));
}

void mcp_client_set_audio_callback(mcp_client_t *c, mcp_audio_callback_t cb, void *user_data)
{
    if (!c) return;
    c->audio_cb = cb;
    c->audio_user_data = user_data;
}

void mcp_client_set_audio_done_callback(mcp_client_t *c, mcp_audio_done_callback_t cb, void *user_data)
{
    if (!c) return;
    c->audio_done_cb = cb;
    c->audio_done_user_data = user_data;
}

/* 简单的 base64 解码（用于音频通知中的 data 字段） */
static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t b64_decode(const char *src, uint8_t *dst, size_t dst_max)
{
    size_t out = 0;
    int acc = 0, bits = 0;
    for (const char *p = src; *p; p++) {
        if (*p == '"' || *p == '}') break;
        int v = b64_val(*p);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out < dst_max) dst[out++] = (uint8_t)(acc >> bits);
        }
    }
    return out;
}

static daima_err_t read_json_line(mcp_client_t *c, char *buf, size_t size, int timeout_ms)
{
    if (!c || !c->out || !buf || size == 0) return DAIMA_ERR_INVALID_ARG;

    int fd = fileno(c->out);
    if (fd < 0) return DAIMA_FAIL;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }

    int n = select(fd + 1, &fds, NULL, NULL, tvp);
    if (n == 0) return DAIMA_ERR_TIMEOUT;
    if (n < 0) {
        if (errno == EINTR) return DAIMA_ERR_TIMEOUT;
        DAIMA_LOGW(TAG, "select failed: %s", strerror(errno));
        return DAIMA_FAIL;
    }

    if (!fgets(buf, size, c->out)) {
        clearerr(c->out);
        return DAIMA_ERR_TIMEOUT;
    }
    return DAIMA_OK;
}

static void handle_audio_notification(mcp_client_t *c, const char *json_str)
{
    if (!c->audio_cb) return;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (!params) { cJSON_Delete(root); return; }

    cJSON *data = cJSON_GetObjectItem(params, "data");
    cJSON *ts   = cJSON_GetObjectItem(params, "timestamp");
    if (!data || !cJSON_IsString(data)) { cJSON_Delete(root); return; }

    uint64_t timestamp = (ts && cJSON_IsNumber(ts)) ? (uint64_t)ts->valuedouble : 0;

    /* base64 解码 */
    uint8_t pcm[MCP_AUDIO_BUF_SIZE];
    size_t len = b64_decode(data->valuestring, pcm, sizeof(pcm));

    cJSON_Delete(root);

    if (len > 0) {
        c->audio_cb(pcm, len, timestamp, c->audio_user_data);
    }
}

static void handle_audio_done_notification(mcp_client_t *c, const char *json_str)
{
    if (!c->audio_done_cb) return;

    mcp_audio_direction_t dir = {0};
    cJSON *root = cJSON_Parse(json_str);
    if (root) {
        cJSON *params = cJSON_GetObjectItem(root, "params");
        if (params) {
            cJSON *v;
            v = cJSON_GetObjectItem(params, "direction");
            if (v && cJSON_IsNumber(v)) dir.direction = (uint16_t)v->valuedouble;
            v = cJSON_GetObjectItem(params, "selectedDirection");
            if (v && cJSON_IsNumber(v)) dir.selectedDirection = (uint16_t)v->valuedouble;
            v = cJSON_GetObjectItem(params, "confidence");
            if (v && cJSON_IsNumber(v)) dir.confidence = (int16_t)v->valuedouble;
            v = cJSON_GetObjectItem(params, "proxDistanceMm");
            if (v && cJSON_IsNumber(v)) dir.prox_distance_mm = (uint32_t)v->valuedouble;
            v = cJSON_GetObjectItem(params, "proxFoundObject");
            if (v) dir.prox_found_object = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(params, "proxUnobstructed");
            if (v) dir.prox_unobstructed = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(params, "cliffDetected");
            if (v) dir.cliff_detected = cJSON_IsTrue(v);
            v = cJSON_GetObjectItem(params, "robotStatus");
            if (v && cJSON_IsNumber(v)) dir.robot_status = (uint32_t)v->valuedouble;
            v = cJSON_GetObjectItem(params, "headAngleDeg");
            if (v && cJSON_IsNumber(v)) dir.head_angle_deg = (float)v->valuedouble;
        }
        cJSON_Delete(root);
    }

    c->audio_done_cb(&dir, c->audio_done_user_data);
}

static bool handle_mcp_notification(mcp_client_t *c, const char *json_str)
{
    if (!c || !json_str) return false;
    if (strstr(json_str, "notifications/audio/chunk")) {
        handle_audio_notification(c, json_str);
        return true;
    }
    if (strstr(json_str, "notifications/audio/done")) {
        handle_audio_done_notification(c, json_str);
        return true;
    }
    return false;
}

static int jsonrpc_response_id(const cJSON *root)
{
    cJSON *id = cJSON_GetObjectItem((cJSON *)root, "id");
    if (!id || !cJSON_IsNumber(id)) return -1;
    return (int)id->valuedouble;
}

static bool is_jsonrpc_response(const cJSON *root)
{
    return cJSON_GetObjectItem((cJSON *)root, "result") ||
           cJSON_GetObjectItem((cJSON *)root, "error");
}

static daima_err_t wait_for_response(mcp_client_t *c, int request_id, int timeout_ms,
                                     char *response, size_t response_size)
{
    if (!c || !response || response_size == 0) return DAIMA_ERR_INVALID_ARG;
    response[0] = '\0';

    while (1) {
        if (c->pending[0] != '\0') {
            snprintf(c->buf, sizeof(c->buf), "%s", c->pending);
            c->pending[0] = '\0';
        } else {
            daima_err_t err = read_json_line(c, c->buf, sizeof(c->buf), timeout_ms);
            if (err != DAIMA_OK) return err;
        }

        if (handle_mcp_notification(c, c->buf)) {
            continue;
        }

        cJSON *root = cJSON_Parse(c->buf);
        if (!root) {
            DAIMA_LOGW(TAG, "Ignoring invalid MCP JSON: %.120s", c->buf);
            continue;
        }

        if (is_jsonrpc_response(root)) {
            int id = jsonrpc_response_id(root);
            if (id == request_id) {
                if (response != c->buf) {
                    snprintf(response, response_size, "%s", c->buf);
                }
                cJSON_Delete(root);
                return DAIMA_OK;
            }
            DAIMA_LOGW(TAG, "Ignoring MCP response id=%d while waiting for id=%d", id, request_id);
        }

        cJSON_Delete(root);
    }
}

int mcp_client_poll(mcp_client_t *c)
{
    if (!c || !c->out) return 0;

    int count = 0;

    pthread_mutex_lock(&c->io_mutex);
    while (read_json_line(c, c->buf, sizeof(c->buf), 0) == DAIMA_OK) {
        if (!handle_mcp_notification(c, c->buf)) {
            snprintf(c->pending, sizeof(c->pending), "%s", c->buf);
        }
        count++;
    }
    pthread_mutex_unlock(&c->io_mutex);
    return count;
}

void mcp_client_close_stdin(mcp_client_t *c)
{
    if (c && c->in) {
        pthread_mutex_lock(&c->io_mutex);
        fclose(c->in);
        c->in = NULL;
        pthread_mutex_unlock(&c->io_mutex);
    }
}
