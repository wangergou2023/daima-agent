/* terminal 执行辅助层。 */

#include "drivers/tool/tool_terminal_exec.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "linux/slab.h"

static bool is_word_boundary(char c)
{
    return c == '\0' || c == ' ' || c == '\t' || c == '\n' ||
           c == ';' || c == '&' || c == '|' || c == '(' || c == ')';
}

bool terminal_command_has_real_sudo(const char *command)
{
    if (!command) return false;
    const char *p = command;
    while ((p = strstr(p, "sudo")) != NULL) {
        char before = (p == command) ? '\0' : *(p - 1);
        char after = *(p + 4);
        if (is_word_boundary(before) && is_word_boundary(after)) {
            return true;
        }
        p += 4;
    }
    return false;
}

char *terminal_rewrite_sudo_command(const char *command)
{
    if (!command) return NULL;
    const char *needle = "sudo";
    const char *replace = "sudo -S -p ''";
    size_t cmd_len = strlen(command);
    size_t rep_len = strlen(replace);

    size_t cap = cmd_len + 64;
    char *out = kzalloc(cap, GFP_KERNEL);
    if (!out) return NULL;
    size_t off = 0;

    const char *p = command;
    while (*p) {
        const char *hit = strstr(p, needle);
        if (!hit) {
            size_t tail = strlen(p);
            if (off + tail + 1 > cap) {
                char *tmp = realloc(out, off + tail + 1);
                if (!tmp) {
                    kfree(out);
                    return NULL;
                }
                out = tmp;
                cap = off + tail + 1;
            }
            memcpy(out + off, p, tail);
            off += tail;
            out[off] = '\0';
            break;
        }

        char before = (hit == command) ? '\0' : *(hit - 1);
        char after = *(hit + 4);
        bool real = is_word_boundary(before) && is_word_boundary(after);

        size_t prefix = (size_t)(hit - p);
        size_t add = prefix + (real ? rep_len : 4);
        if (off + add + 1 > cap) {
            size_t new_cap = cap * 2;
            while (off + add + 1 > new_cap) new_cap *= 2;
            char *tmp = realloc(out, new_cap);
            if (!tmp) {
                kfree(out);
                return NULL;
            }
            out = tmp;
            cap = new_cap;
        }

        memcpy(out + off, p, prefix);
        off += prefix;
        if (real) {
            memcpy(out + off, replace, rep_len);
            off += rep_len;
        } else {
            memcpy(out + off, hit, 4);
            off += 4;
        }
        out[off] = '\0';
        p = hit + 4;
    }

    return out;
}

char *terminal_json_result_string(const char *command,
                                  const char *workdir,
                                  const terminal_exec_result_t *result,
                                  const char *error_text)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "command", command ? command : "");
    cJSON_AddStringToObject(root, "workdir", workdir ? workdir : "");
    cJSON_AddNumberToObject(root, "exit_code", result ? result->exit_code : -1);
    cJSON_AddBoolToObject(root, "timed_out", result ? result->timed_out : false);
    cJSON_AddBoolToObject(root, "truncated", result ? result->truncated : false);
    cJSON_AddNumberToObject(root, "signal", result ? result->signal_num : 0);
    cJSON_AddStringToObject(root, "output", (result && result->output) ? result->output : "");
    if (error_text && error_text[0]) {
        cJSON_AddStringToObject(root, "error", error_text);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

char *terminal_json_status_string(const char *command,
                                  const char *workdir,
                                  const char *status,
                                  const char *message,
                                  const char *request_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "command", command ? command : "");
    cJSON_AddStringToObject(root, "workdir", workdir ? workdir : "");
    cJSON_AddStringToObject(root, "status", status ? status : "");
    cJSON_AddStringToObject(root, "message", message ? message : "");
    if (request_id && request_id[0]) {
        cJSON_AddStringToObject(root, "request_id", request_id);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

static void kill_process_group(pid_t pid)
{
    if (pid <= 0) {
        return;
    }
    kill(-pid, SIGTERM);
    usleep(150 * 1000);
    kill(-pid, SIGKILL);
}

err_t terminal_execute_local_shell(const char *command,
                                        const char *workdir,
                                        int timeout_seconds,
                                        const char *stdin_data,
                                        size_t output_cap,
                                        terminal_exec_result_t *out)
{
    if (!command || !command[0] || !out) {
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->exit_code = -1;
    out->output = kzalloc(output_cap > 0 ? output_cap : 1, GFP_KERNEL);
    if (!out->output) {
        return ERR_NO_MEM;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        kfree(out->output);
        out->output = NULL;
        return ERR_FAIL;
    }

    int stdin_pipe[2] = {-1, -1};
    bool use_stdin = stdin_data && stdin_data[0];
    if (use_stdin && pipe(stdin_pipe) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        kfree(out->output);
        out->output = NULL;
        return ERR_FAIL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);
        if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
        kfree(out->output);
        out->output = NULL;
        return ERR_FAIL;
    }

    if (pid == 0) {
        close(pipefd[0]);
        setpgid(0, 0);

        if (workdir && workdir[0] && chdir(workdir) != 0) {
            dprintf(pipefd[1], "chdir failed: %s\n", strerror(errno));
            _exit(125);
        }

        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (use_stdin) {
            close(stdin_pipe[1]);
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
        }

        execl("/bin/bash", "bash", "-lc", command, (char *)NULL);
        dprintf(STDERR_FILENO, "exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);
    if (use_stdin) {
        close(stdin_pipe[0]);
        size_t stdin_len = strlen(stdin_data);
        ssize_t wrote = write(stdin_pipe[1], stdin_data, stdin_len);
        (void)wrote;
        close(stdin_pipe[1]);
    }
    set_nonblocking(pipefd[0]);

    size_t off = 0;
    bool child_done = false;
    time_t start = time(NULL);
    int status = 0;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(pipefd[0], &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200 * 1000 };
        int ready = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);

        if (ready > 0 && FD_ISSET(pipefd[0], &rfds)) {
            char buf[512];
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                size_t copy = (size_t)n;
                if (off + copy >= output_cap) {
                    if (off < output_cap - 1) {
                        copy = (output_cap - 1) - off;
                        memcpy(out->output + off, buf, copy);
                        off += copy;
                    }
                    out->truncated = true;
                } else {
                    memcpy(out->output + off, buf, copy);
                    off += copy;
                }
                out->output[off] = '\0';
            }
        }

        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            child_done = true;
        }

        if (!child_done && timeout_seconds > 0 && (int)(time(NULL) - start) >= timeout_seconds) {
            out->timed_out = true;
            kill_process_group(pid);
            waitpid(pid, &status, 0);
            child_done = true;
        }

        if (child_done) {
            char drain[512];
            while (1) {
                ssize_t n = read(pipefd[0], drain, sizeof(drain));
                if (n <= 0) {
                    break;
                }
                size_t copy = (size_t)n;
                if (off + copy >= output_cap) {
                    if (off < output_cap - 1) {
                        copy = (output_cap - 1) - off;
                        memcpy(out->output + off, drain, copy);
                        off += copy;
                    }
                    out->truncated = true;
                } else {
                    memcpy(out->output + off, drain, copy);
                    off += copy;
                }
                out->output[off] = '\0';
            }
            break;
        }
    }

    close(pipefd[0]);

    if (out->timed_out) {
        out->exit_code = 124;
    } else if (WIFEXITED(status)) {
        out->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        out->signal_num = WTERMSIG(status);
        out->exit_code = 128 + out->signal_num;
    }

    if (off == 0) {
        snprintf(out->output, output_cap, "（无输出）");
    } else if (out->truncated) {
        const char *suffix = "\n...（输出过长已截断）";
        size_t suffix_len = strlen(suffix);
        size_t cur_len = strlen(out->output);
        if (cur_len + suffix_len < output_cap) {
            memcpy(out->output + cur_len, suffix, suffix_len + 1);
        }
    }

    return 0;
}
