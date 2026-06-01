/* MCP JSON-RPC 2.0 客户端实现
 * 通过 fork+exec+pipe 启动 robot-mcp 子进程，双向通信。
 */
#include "channels/vector/mcp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include "cJSON.h"
#include "daima_log.h"
#include "daima_config.h"

static void handle_audio_notification(mcp_client_t *c, const char *json_str);

static const char *TAG = "mcp_client";

struct mcp_client {
    FILE   *in;            /* 子进程 stdin  (写入) */
    FILE   *out;           /* 子进程 stdout (读取) */
    pid_t   pid;
    int     request_id;
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
    cJSON_AddStringToObject(init_req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(init_req, "id", (double)(++c->request_id));
    cJSON_AddStringToObject(init_req, "method", "initialize");
    cJSON_AddItemToObject(init_req, "params", init_params);

    char *init_str = cJSON_PrintUnformatted(init_req);
    cJSON_Delete(init_req);

    fprintf(c->in, "%s\n", init_str);
    fflush(c->in);
    free(init_str);

    /* 读取 initialize 响应 — robot-mcp 可能需要几秒连接 gRPC */
    {
        int retries = 0;
        while (retries < 60) {
            usleep(500000);
            if (fgets(c->buf, sizeof(c->buf), c->out)) {
                break;
            }
            clearerr(c->out);
            retries++;
        }
    }
    if (c->buf[0] == '\0') {
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
    if (c->in) {
        /* Tell robot-mcp to unsubscribe and stop */
        fprintf(c->in, "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"tools/call\",\"params\":{\"name\":\"robot_unsubscribe_audio\",\"arguments\":{}}}\n");
        fflush(c->in);
        fclose(c->in);
    }
    if (c->out) fclose(c->out);
    if (c->pid > 0) {
        /* Give child a moment to exit gracefully, then kill */
        int status;
        kill(c->pid, SIGTERM);
        waitpid(c->pid, &status, 0);
    }
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
    cJSON_AddStringToObject(params, "name", tool_name);
    cJSON *args = cJSON_Parse(args_json ? args_json : "{}");
    if (!args) args = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "arguments", args);

    /* 加锁保护 stdin/stdout 和 request_id */
    flockfile(c->in);
    flockfile(c->out);

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", (double)(++c->request_id));
    cJSON_AddStringToObject(req, "method", "tools/call");
    cJSON_AddItemToObject(req, "params", params);

    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    fprintf(c->in, "%s\n", req_str);
    fflush(c->in);
    free(req_str);

    /* 读取响应 */
    response_out[0] = '\0';

    /* Read lines until we get a valid tools/call response */
    while (1) {
        if (c->pending[0] != '\0') {
            strncpy(c->buf, c->pending, sizeof(c->buf) - 1);
            c->buf[sizeof(c->buf) - 1] = '\0';
            c->pending[0] = '\0';
        } else if (!fgets(c->buf, sizeof(c->buf), c->out)) {
            funlockfile(c->in);
            funlockfile(c->out);
            DAIMA_LOGE(TAG, "No response to tools/call %s", tool_name);
            snprintf(response_out, response_size, "Error: no response");
            return DAIMA_ERR_TIMEOUT;
        }

        /* Check if this line is a tools/call response */
        if (strstr(c->buf, "\"result\"") || strstr(c->buf, "\"error\"")) break;

        /* Not a response — process any notifications in-line so they aren't lost */
        if (strstr(c->buf, "notifications/audio/chunk")) {
            handle_audio_notification(c, c->buf);
        } else if (strstr(c->buf, "notifications/audio/done")) {
            if (c->audio_done_cb) {
                mcp_audio_direction_t dir = {0};
                cJSON *root = cJSON_Parse(c->buf);
                if (root) {
                    cJSON *params = cJSON_GetObjectItem(root, "params");
                    if (params) {
                        cJSON *v;
                        #define G16(f,n) v = cJSON_GetObjectItem(params, n); if (v && cJSON_IsNumber(v)) dir.f = (uint16_t)v->valuedouble
                        #define G32(f,n) v = cJSON_GetObjectItem(params, n); if (v && cJSON_IsNumber(v)) dir.f = (uint32_t)v->valuedouble
                        #define GB(f,n)  v = cJSON_GetObjectItem(params, n); if (v) dir.f = cJSON_IsTrue(v)
                        #define GF(f,n)  v = cJSON_GetObjectItem(params, n); if (v && cJSON_IsNumber(v)) dir.f = (float)v->valuedouble
                        G16(direction,"direction");G16(selectedDirection,"selectedDirection");
                        v=cJSON_GetObjectItem(params,"confidence");if(v&&cJSON_IsNumber(v))dir.confidence=(int16_t)v->valuedouble;
                        G32(prox_distance_mm,"proxDistanceMm");
                        GB(prox_found_object,"proxFoundObject");GB(prox_unobstructed,"proxUnobstructed");
                        GB(cliff_detected,"cliffDetected");G32(robot_status,"robotStatus");GF(head_angle_deg,"headAngleDeg");
                        #undef G16 #undef G32 #undef GB #undef GF
                    }
                    cJSON_Delete(root);
                }
                c->audio_done_cb(&dir, c->audio_done_user_data);
            }
        }
    }
    funlockfile(c->in);
    funlockfile(c->out);

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

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", (double)(++c->request_id));
    cJSON_AddStringToObject(req, "method", "tools/list");

    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    flockfile(c->in);
    flockfile(c->out);
    fprintf(c->in, "%s\n", req_str);
    fflush(c->in);
    free(req_str);

    if (!fgets(c->buf, sizeof(c->buf), c->out)) {
        funlockfile(c->in);
        funlockfile(c->out);
        tools_json_out[0] = '\0';
        return DAIMA_ERR_TIMEOUT;
    }
    funlockfile(c->in);
    funlockfile(c->out);

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
    char resp[256];
    return mcp_client_call_tool(c, "robot_subscribe_audio", "{}", resp, sizeof(resp));
}

daima_err_t mcp_client_unsubscribe_audio(mcp_client_t *c)
{
    char resp[256];
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

int mcp_client_poll(mcp_client_t *c)
{
    if (!c || !c->out) return 0;

    int count = 0;
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fileno(c->out), &fds);

    while (select(fileno(c->out) + 1, &fds, NULL, NULL, &tv) > 0) {
        memset(c->buf, 0, sizeof(c->buf));
        if (!fgets(c->buf, sizeof(c->buf), c->out)) return count;

        /* 检查是否是音频通知 */
        if (strstr(c->buf, "notifications/audio/chunk")) {
            handle_audio_notification(c, c->buf);
        } else if (strstr(c->buf, "notifications/audio/done")) {
            if (c->audio_done_cb) {
                mcp_audio_direction_t dir = {0};
                cJSON *root = cJSON_Parse(c->buf);
                if (root) {
                    cJSON *params = cJSON_GetObjectItem(root, "params");
                    if (params) {
                        cJSON *v;
                        #define GET_U16(field, name) v = cJSON_GetObjectItem(params, name); if (v && cJSON_IsNumber(v)) dir.field = (uint16_t)v->valuedouble
                        #define GET_U32(field, name) v = cJSON_GetObjectItem(params, name); if (v && cJSON_IsNumber(v)) dir.field = (uint32_t)v->valuedouble
                        #define GET_BOOL(field, name) v = cJSON_GetObjectItem(params, name); if (v) dir.field = cJSON_IsTrue(v)
                        #define GET_FLOAT(field, name) v = cJSON_GetObjectItem(params, name); if (v && cJSON_IsNumber(v)) dir.field = (float)v->valuedouble
                        GET_U16(direction, "direction");
                        GET_U16(selectedDirection, "selectedDirection");
                        v = cJSON_GetObjectItem(params, "confidence");
                        if (v && cJSON_IsNumber(v)) dir.confidence = (int16_t)v->valuedouble;
                        GET_U32(prox_distance_mm, "proxDistanceMm");
                        GET_BOOL(prox_found_object, "proxFoundObject");
                        GET_BOOL(prox_unobstructed, "proxUnobstructed");
                        GET_BOOL(cliff_detected, "cliffDetected");
                        GET_U32(robot_status, "robotStatus");
                        GET_FLOAT(head_angle_deg, "headAngleDeg");
                        #undef GET_U16
                        #undef GET_U32
                        #undef GET_BOOL
                        #undef GET_FLOAT
                    }
                    cJSON_Delete(root);
                }
                c->audio_done_cb(&dir, c->audio_done_user_data);
            }
        } else {
            /* Might be a tools/call response — save for mcp_client_call_tool */
            strncpy(c->pending, c->buf, sizeof(c->pending) - 1);
            c->pending[sizeof(c->pending) - 1] = '\0';
        }
        count++;
    }
    return count;
}

void mcp_client_close_stdin(mcp_client_t *c)
{
    if (c && c->in) {
        fclose(c->in);
        c->in = NULL;
    }
}
