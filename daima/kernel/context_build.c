/* 系统提示构建与上下文拼装。 */

#include "context_build.h"
#include "paths.h"
#include "autoconf.h"
#include "drivers/memory/memory_store.h"
#include "drivers/skill/skill_loader.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "linux/printk.h"
static size_t append_textf(char *buf, size_t size, size_t offset, const char *fmt, ...);

/* Strip any orphaned multi-byte UTF-8 sequences anywhere in the buffer.
 * vsnprintf/fread truncation can leave lead bytes without continuation bytes. */
static void fix_truncated_utf8(char *buf, size_t len)
{
    if (len < 2) return;
    size_t out = 0, pos = 0;
    while (pos < len) {
        unsigned char b = (unsigned char)buf[pos];
        if (b < 0x80) {
            buf[out++] = buf[pos++];    /* ASCII, pass through */
        } else if (b < 0xC0) {
            pos++;                      /* Stray continuation byte, skip */
        } else if (b < 0xE0) {
            if (pos + 1 < len && (buf[pos+1] & 0xC0) == 0x80) {
                buf[out++] = buf[pos++];
                buf[out++] = buf[pos++];
            } else {
                pos++;                  /* Orphaned 2-byte start, skip */
            }
        } else if (b < 0xF0) {
            if (pos + 2 < len && (buf[pos+1] & 0xC0) == 0x80 && (buf[pos+2] & 0xC0) == 0x80) {
                buf[out++] = buf[pos++];
                buf[out++] = buf[pos++];
                buf[out++] = buf[pos++];
            } else {
                pos++;                  /* Orphaned 3-byte start, skip */
            }
        } else if (b < 0xF5) {
            if (pos + 3 < len && (buf[pos+1] & 0xC0) == 0x80 && (buf[pos+2] & 0xC0) == 0x80 && (buf[pos+3] & 0xC0) == 0x80) {
                buf[out++] = buf[pos++];
                buf[out++] = buf[pos++];
                buf[out++] = buf[pos++];
                buf[out++] = buf[pos++];
            } else {
                pos++;                  /* Orphaned 4-byte start, skip */
            }
        } else {
            pos++;                      /* Invalid byte, skip */
        }
    }
    buf[out] = '\0';
}

void context_fix_truncated_utf8(char *buf, size_t len)
{
    fix_truncated_utf8(buf, len);
}

static bool file_has_content(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    int ch = fgetc(f);
    fclose(f);
    return ch != EOF;
}

static bool file_exists(const char *path)
{
    return path && access(path, F_OK) == 0;
}

static bool file_exists_under(const char *root, const char *name)
{
    if (!root || !root[0] || !name || !name[0]) return false;
    char path[1200];
    if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path)) {
        return false;
    }
    return file_exists(path);
}

static bool read_process_output(char *out, size_t out_size, const char *program, char *const argv[])
{
    if (!out || out_size == 0 || !program || !argv) return false;
    out[0] = '\0';

    int pipefd[2];
    if (pipe(pipefd) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(program, argv);
        _exit(127);
    }

    close(pipefd[1]);
    ssize_t n = read(pipefd[0], out, out_size - 1);
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);

    if (n <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        out[0] = '\0';
        return false;
    }
    out[n] = '\0';

    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' ')) {
        out[--len] = '\0';
    }
    return out[0] != '\0';
}

static bool read_git_first_line(char *out, size_t out_size, char *const argv[])
{
    if (!read_process_output(out, out_size, "git", argv)) {
        return false;
    }
    char *newline = strchr(out, '\n');
    if (newline) {
        *newline = '\0';
    }
    return out[0] != '\0';
}

static int run_process_quiet(const char *program, char *const argv[])
{
    if (!program || !argv) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(program, argv);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

static void append_stack_item(char *stack, size_t stack_size, const char *item)
{
    if (!stack || stack_size == 0 || !item || !item[0]) return;
    if (stack[0]) {
        strncat(stack, ", ", stack_size - strlen(stack) - 1);
    }
    strncat(stack, item, stack_size - strlen(stack) - 1);
}

static size_t append_workspace_context(char *buf, size_t size, size_t offset)
{
    char cwd[BUF_LARGE];
    if (!getcwd(cwd, sizeof(cwd))) {
        return offset;
    }

    char branch[128] = {0};
    char repo_root[BUF_LARGE] = {0};
    char commit[256] = {0};
    char *branch_args[] = {"git", "branch", "--show-current", NULL};
    char *root_args[] = {"git", "rev-parse", "--show-toplevel", NULL};
    char *status_args[] = {"git", "diff-index", "--quiet", "HEAD", "--", NULL};
    char *commit_args[] = {"git", "log", "--oneline", "-1", NULL};
    bool has_branch = read_git_first_line(branch, sizeof(branch), branch_args);
    bool has_repo_root = read_git_first_line(repo_root, sizeof(repo_root), root_args);
    int status_exit = run_process_quiet("git", status_args);
    bool has_status = status_exit == 0 || status_exit == 1;
    bool is_dirty = status_exit == 1;
    bool has_commit = read_git_first_line(commit, sizeof(commit), commit_args);
    bool has_git = has_branch || has_repo_root || has_status || has_commit;

    const char *project_root = has_repo_root ? repo_root : cwd;
    char stack[256] = {0};
    if (file_exists_under(project_root, "CMakeLists.txt")) append_stack_item(stack, sizeof(stack), "C/CMake");
    if (file_exists_under(project_root, "package.json")) append_stack_item(stack, sizeof(stack), "Node.js");
    if (file_exists_under(project_root, "tsconfig.json")) append_stack_item(stack, sizeof(stack), "TypeScript");
    if (file_exists_under(project_root, "go.mod")) append_stack_item(stack, sizeof(stack), "Go");
    if (file_exists_under(project_root, "Cargo.toml")) append_stack_item(stack, sizeof(stack), "Rust");
    if (file_exists_under(project_root, "pyproject.toml") || file_exists_under(project_root, "requirements.txt")) append_stack_item(stack, sizeof(stack), "Python");
    if (file_exists_under(project_root, "docker-compose.yml") || file_exists_under(project_root, "Dockerfile")) append_stack_item(stack, sizeof(stack), "Docker");

    offset = append_textf(buf, size, offset, "\n## 当前工作区\n\n");
    offset = append_textf(buf, size, offset, "- cwd: `%s`\n", cwd);
    offset = append_textf(buf, size, offset, "- daima workspace: `%s`\n", path_workspace_dir());
    offset = append_textf(buf, size, offset, "- 工具默认工作目录是 daima workspace；安装依赖、生成临时脚本和未指定路径的新文件应放在 daima workspace，不要污染 cwd 或 repo。\n");
    if (has_repo_root) {
        offset = append_textf(buf, size, offset, "- repo root: `%s`\n", repo_root);
    }
    if (has_git) {
        offset = append_textf(buf, size, offset, "- git:");
        if (has_branch) offset = append_textf(buf, size, offset, " branch `%s`", branch);
        if (has_status) offset = append_textf(buf, size, offset, " %s", is_dirty ? "dirty" : "clean");
        if (has_commit) offset = append_textf(buf, size, offset, " latest `%s`", commit);
        offset = append_textf(buf, size, offset, "\n");
    }
    if (stack[0]) {
        offset = append_textf(buf, size, offset, "- stack: %s\n", stack);
    }
    return offset;
}

static size_t append_textf(char *buf, size_t size, size_t offset, const char *fmt, ...)
{
    if (!buf || size == 0 || offset >= size - 1 || !fmt) {
        return offset;
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + offset, size - offset, fmt, ap);
    va_end(ap);
    if (n < 0) {
        buf[size - 1] = '\0';
        return offset;
    }

    size_t written = (size_t)n;
    if (written >= size - offset) {
        buf[size - 1] = '\0';
        return size - 1;
    }
    return offset + written;
}

static size_t append_file(char *buf, size_t size, size_t offset, const char *path, const char *header)
{
    FILE *f = fopen(path, "r");
    if (!f) return offset;

    if (header && offset < size - 1) {
        offset = append_textf(buf, size, offset, "\n## %s\n\n", header);
    }

    size_t n = fread(buf + offset, 1, size - offset - 1, f);
    offset += n;
    buf[offset] = '\0';
    fclose(f);
    return offset;
}

static size_t append_operator_guide_fallback(char *buf, size_t size, size_t offset, bool has_bootstrap)
{
    offset = append_textf(
        buf, size, offset,
        "\n## 角色与运行环境\n\n"
        "%s"
        "- 你通过 WebSocket、Web 宠物互动事件或飞书与用户交互。\n"
        "- 你可以读取本地文件、调用本地工具、维护记忆与技能。\n"
        "- 先理解当前任务，再决定是否调用工具；用完工具后给出清晰最终回答。\n",
        has_bootstrap ? "" : "你是代马（Daima），一个运行在 Linux 本地进程中的个人 AI 助手。\n");

    offset = append_textf(
        buf, size, offset,
        "\n## 工作方式\n\n"
        "1. 优先理解用户真实目标，不做机械追问。\n"
        "2. 需要行动时使用工具；需要修改已有文件时，先看上下文再改。\n"
        "3. 分析代码时，先缩小范围：优先用 `files` 的 `action=search`（`output_mode=files_only/count`、`file_glob`、`path`）找候选文件，再用 `files action=read` 分页深读，不要一上来把很多文件整份读完。\n"
        "4. 新建、修改、删除文本文件时，默认顺序是：先用 `files action=search/read` 看清上下文，再用 `apply_patch`。\n"
        "5. `terminal` 适合安装工具、构建、运行命令、看 git 或进程；不适合替代 `files` / `apply_patch` 做文件查看或文本修改。\n"
        "6. 调用 `terminal` 时，安装软件、更新索引、构建大项目要主动设置更长 `timeout`；看到结构化结果后，要基于 `exit_code`、`timed_out`、`output` 判断是否真的成功。\n"
        "7. 遇到 bug、功能缺失、体验问题时，先加载 `Work Item 收集` 技能，用 `work_item` 工具记录而非直接修复。\n"
        "8. 当使用 `cron action=add` 发送到 WebSocket 或飞书时，务必设置 `channel='websocket'` 或 `channel='feishu'` 并提供有效 `chat_id`。\n");

    offset = append_textf(
        buf, size, offset,
        "\n## 工具与参数命名约定\n\n"
        "- 说明文字可以用中文，但真正要调用的工具名、函数名、参数名、字段名必须保持英文原样。\n"
        "- 当你决定调用工具时，必须使用 schema 里的真实标识符，例如 `files`、`action`、`cron`、`channel`、`chat_id`、`exit_code`、`output_mode`。\n"
        "- 不要把工具名或参数名翻译成中文后再调用，也不要自造不存在的中文字段。\n"
        "- 不要调用外部或其他 Agent 的工具名，例如 `todolist__add`、`todo_write`、`apply_patch_file`；待办只能用 `todo`，文件修改只能用 `apply_patch`。\n"
        "- 最稳妥的表达方式是：中文解释 + 英文标识符并列出现。\n");

    offset = append_textf(
        buf, size, offset,
        "\n## 可用工具速览\n\n"
        "- `weather`：查询当前天气与预报。\n"
        "- `get_current_time`：获取当前日期和时间；你没有内置时钟，需要时间时必须调用。\n"
        "- `files`：统一文件查看工具；`action=read` 分页读文件，`action=list` 列目录，`action=search` 搜文件名或文本内容。\n"
        "- `apply_patch`：Codex 风格补丁，新建、修改、删除文本文件的唯一文件修改工具。\n"
        "- `todo`：管理待办列表。\n"
        "- `work_item`：收集和管理结构化事项，覆盖 defect / missing / improvement / tech_debt / docs / test_gap。\n"
        "- `webfetch`：获取网页内容（text/html），用于搜索信息、阅读文档。\n"
        "- `log_tool`：读取 daima 自身运行日志（tail/search/errors），用于诊断工具失败和系统异常。\n"
        "- `skills`：查看技能；`action=list` 看总览，`action=view` 读技能说明。\n"
        "- `session_search`：搜索历史会话、压缩摘要和事实卡片。\n"
        "- `terminal`：执行本地 shell 命令，返回包含 `output`、`exit_code`、`timed_out`、`workdir` 的 JSON。\n"
        "- `cron`：管理定时任务；`action=add/list/remove`。\n");

    return offset;
}

static size_t append_dynamic_runtime_guide_fallback(char *buf, size_t size, size_t offset)
{
    offset = append_textf(
        buf, size, offset,
        "\n## 记忆与引导文件\n\n"
        "### 持久化记忆\n"
        "- 长期记忆：`%s`\n"
        "- 每日笔记：`%s/<YYYY-MM-DD>.md`\n"
        "- 更新记忆前先用 `files action=read`，再用 `apply_patch` 做最小改动；写每日笔记前先 `get_current_time`。\n\n"
        "### 可读取与按需更新的引导文件\n"
        "- Bootstrap：`%s/BOOTSTRAP.md`\n"
        "- Identity：`%s/IDENTITY.md`\n"
        "- Personality：`%s`\n"
        "- User Info：`%s`\n"
        "- 更新这些文件时，先用 `files action=search/read` 看上下文，再用 `apply_patch` 做最小改动，避免直接覆盖。\n"
        "- 若文件不存在，用 `apply_patch` 的 `*** Add File` 创建。\n",
        path_memory_dir(),
        path_memory_dir(),
        path_config_dir(),
        path_config_dir(),
        path_soul_file(),
        path_user_file());

    return append_textf(
        buf, size, offset,
        "\n## 技能使用规则\n\n"
        "- 技能文件位于 `%s` 下。\n"
        "- 优先用 `skills action=list` 查看总览，再用 `skills action=view` 按名称读取完整说明。\n"
        "- 你可以用 `apply_patch` 创建新技能到 `%s/<name>/SKILL.md`。\n"
        "- 如果只是修改已有技能，先用 `files action=read`，再用 `apply_patch`。\n"
        "- 技能文件必须包含 YAML front matter 的 `name` 和 `description`，否则无法加载。\n",
        path_skills_dir(),
        path_skills_dir());
}

err_t context_build_system_prompt_for_channel(const char *channel, char *buf, size_t size)
{
    size_t off = 0;
    bool has_bootstrap = file_has_content(path_bootstrap_file());

    off = append_textf(
        buf, size, off,
        "# 代马 Daima\n\n"
        "> 这是当前轮对话的系统说明。把它当作一份长期有效的操作手册；若与用户当前这轮的明确新指令冲突，以用户当前新指令为准。\n");

    if (has_bootstrap) {
        off = append_file(buf, size, off, path_bootstrap_file(), "Bootstrap");
    }

    off = append_operator_guide_fallback(buf, size, off, has_bootstrap);
    off = append_dynamic_runtime_guide_fallback(buf, size, off);
    off = append_workspace_context(buf, size, off);

    /* 身份与用户配置 */
    off = append_file(buf, size, off, path_identity_file(), "身份设定");
    off = append_file(buf, size, off, path_soul_file(), "个性设定");
    off = append_file(buf, size, off, path_user_file(), "用户信息");

    /* 长期记忆 */
    char mem_buf[4096];
    if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == 0 && mem_buf[0]) {
        off = append_textf(buf, size, off, "\n## 长期记忆\n\n%s\n", mem_buf);
    }

    /* 最近的每日笔记（最近 3 天） */
    char recent_buf[4096];
    if (memory_read_recent(recent_buf, sizeof(recent_buf), 3) == 0 && recent_buf[0]) {
        off = append_textf(buf, size, off, "\n## 最近笔记\n\n%s\n", recent_buf);
    }

    /* 技能 */
    char skills_buf[16 * 1024];
    size_t skills_len = skill_loader_build_summary_for_channel(channel, skills_buf, sizeof(skills_buf));
    if (skills_len > 0) {
        off = append_textf(
            buf, size, off,
            "\n## 可用技能摘要\n\n"
            "下面是技能总览。若某个任务明显命中某项技能，请用相关工具读取对应技能文件全文后再执行。\n"
            "若技能文件引用相对路径，应以该技能目录为基准解析后再用于工具调用。\n\n"
            "%s\n",
            skills_buf);
    }

    fix_truncated_utf8(buf, off);
    pr_debug("System prompt built: %d bytes", (int)off);
    return 0;
}

err_t context_build_system_prompt(char *buf, size_t size)
{
    return context_build_system_prompt_for_channel(NULL, buf, size);
}
