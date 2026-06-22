/* 系统提示构建与上下文拼装。 */

#include "context_build.h"
#include "paths.h"
#include "context_sections.h"
#include "guide_paths.h"
#include "workspace_probe.h"
#include "autoconf.h"
#include "drivers/skill/skill_loader.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
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

static size_t append_workspace_context(char *buf, size_t size, size_t offset)
{
    workspace_probe_result_t probe;

    if (!workspace_probe_collect(&probe)) {
        return offset;
    }

    offset = append_textf(buf, size, offset, "\n## 当前工作区\n\n");
    offset = append_textf(buf, size, offset, "- cwd: `%s`\n", probe.cwd);
    offset = append_textf(buf, size, offset, "- agent workspace: `%s`\n", probe.agent_workspace);
    offset = append_textf(buf, size, offset, "- 工具默认工作目录是 agent workspace；安装依赖、生成临时脚本和未指定路径的新文件应放在 agent workspace，不要污染 cwd 或 repo。\n");
    if (probe.has_repo_root) {
        offset = append_textf(buf, size, offset, "- repo root: `%s`\n", probe.repo_root);
    }
    if (probe.has_git) {
        offset = append_textf(buf, size, offset, "- git:");
        if (probe.has_branch) offset = append_textf(buf, size, offset, " branch `%s`", probe.branch);
        if (probe.has_status) offset = append_textf(buf, size, offset, " %s", probe.is_dirty ? "dirty" : "clean");
        if (probe.has_commit) offset = append_textf(buf, size, offset, " latest `%s`", probe.commit);
        offset = append_textf(buf, size, offset, "\n");
    }
    if (probe.stack[0]) {
        offset = append_textf(buf, size, offset, "- stack: %s\n", probe.stack);
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

static size_t append_operator_guide_fallback(char *buf, size_t size, size_t offset, bool has_bootstrap)
{
    offset = append_textf(
        buf, size, offset,
        "\n## 角色与运行环境\n\n"
        "%s"
        "- 你通过 WebSocket、Web 宠物互动事件或飞书与用户交互。\n"
        "- 你可以读取本地文件、调用本地工具、维护记忆与技能。\n"
        "- 先理解当前任务，再决定是否调用工具；用完工具后给出清晰最终回答。\n",
        has_bootstrap ? "" : "你是Agent，一个运行在 Linux 本地进程中的个人 AI 助手。\n");

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
        "- `log_tool`：读取 agent 自身运行日志（tail/search/errors），用于诊断工具失败和系统异常。\n"
        "- `skills`：查看技能；`action=list` 看总览，`action=view` 读技能说明。\n"
        "- `session_search`：搜索历史会话、压缩摘要和事实卡片。\n"
        "- `terminal`：执行本地 shell 命令，返回包含 `output`、`exit_code`、`timed_out`、`workdir` 的 JSON。\n"
        "- `cron`：管理定时任务；`action=add/list/remove`。\n");

    return offset;
}

err_t context_build_system_prompt_for_channel(const char *channel, char *buf, size_t size)
{
    size_t off = 0;
    guide_paths_t guide_paths;
    guide_paths_init(&guide_paths);

    bool has_bootstrap = file_has_content(guide_paths.bootstrap_path);

    off = append_textf(
        buf, size, off,
        "# Agent\n\n"
        "> 这是当前轮对话的系统说明。把它当作一份长期有效的操作手册；若与用户当前这轮的明确新指令冲突，以用户当前新指令为准。\n");

    if (has_bootstrap) {
        off = context_sections_append_file(buf, size, off, guide_paths.bootstrap_path, "Bootstrap");
    }

    off = append_operator_guide_fallback(buf, size, off, has_bootstrap);
    off = guide_paths_append_runtime_guide(buf, size, off, &guide_paths);
    off = append_workspace_context(buf, size, off);

    off = context_sections_append_identity(buf, size, off, &guide_paths);
    off = context_sections_append_memory(buf, size, off);

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
