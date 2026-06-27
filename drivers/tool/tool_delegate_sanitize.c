#include "drivers/tool/tool_delegate_sanitize.h"

#include <string.h>

#include "linux/kernel.h"

static bool text_has_any_keyword(const char *text, const char *const *keywords, size_t count)
{
    if (!text || !text[0]) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (keywords[i] && strstr(text, keywords[i])) {
            return true;
        }
    }
    return false;
}

static void trim_trailing_ascii_space(char *text)
{
    if (!text) {
        return;
    }
    size_t len = strlen(text);
    while (len > 0) {
        char ch = text[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        text[--len] = '\0';
    }
}

static void strip_block_between_markers_inplace(char *text,
                                                const char *open_marker,
                                                const char *close_marker)
{
    if (!text || !open_marker || !close_marker) {
        return;
    }

    size_t close_len = strlen(close_marker);
    char *start = strstr(text, open_marker);
    while (start) {
        char *end = strstr(start + strlen(open_marker), close_marker);
        if (!end) {
            *start = '\0';
            trim_trailing_ascii_space(text);
            return;
        }
        end += close_len;
        memmove(start, end, strlen(end) + 1);
        start = strstr(start, open_marker);
    }
}

static void strip_single_line_tag_prefix_inplace(char *text, const char *tag_prefix)
{
    if (!text || !tag_prefix) {
        return;
    }
    char *start = strstr(text, tag_prefix);
    while (start) {
        char *end = strchr(start, '\n');
        if (!end) {
            *start = '\0';
            trim_trailing_ascii_space(text);
            return;
        }
        memmove(start, end + 1, strlen(end + 1) + 1);
        start = strstr(start, tag_prefix);
    }
}

static void strip_inline_transcript_suffix_inplace(char *text)
{
    static const char *const transcript_markers[] = {
        "\nFILE: ",
        "\nSEARCH: ",
        "\n<bash>",
        "\n<fileio>",
        "\n<tool>",
        "\n<read-file",
        "\n```bash",
        "\n```json",
        "\n```shell",
    };

    if (!text) {
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(transcript_markers); i++) {
        char *marker = strstr(text, transcript_markers[i]);
        if (marker) {
            *marker = '\0';
        }
    }
    trim_trailing_ascii_space(text);
}

static bool tool_delegate_text_has_transcript_markup(const char *text)
{
    static const char *const transcript_markers[] = {
        "<bash>",
        "</bash>",
        "<fileio>",
        "</fileio>",
        "<tool>",
        "</tool>",
        "<read-file",
        "```bash",
        "```json",
        "```shell",
        "\nFILE: ",
        "\nSEARCH: ",
        "\nLINES: ",
    };
    return text_has_any_keyword(text, transcript_markers, ARRAY_SIZE(transcript_markers));
}

bool tool_delegate_text_has_dsml_markup(const char *text)
{
    static const char *const dsml_markers[] = {
        "<｜｜DSML｜｜tool_calls>",
        "<｜｜DSML｜｜invoke ",
        "<｜｜DSML｜｜parameter ",
    };
    return text_has_any_keyword(text, dsml_markers, ARRAY_SIZE(dsml_markers));
}

bool tool_delegate_text_has_transcript_markup_public(const char *text)
{
    return tool_delegate_text_has_transcript_markup(text);
}

void tool_delegate_sanitize_summary_text_inplace(char *text)
{
    if (!text || !text[0]) {
        return;
    }

    strip_block_between_markers_inplace(text, "<bash>", "</bash>");
    strip_block_between_markers_inplace(text, "<fileio>", "</fileio>");
    strip_block_between_markers_inplace(text, "<tool>", "</tool>");
    strip_single_line_tag_prefix_inplace(text, "<read-file");
    strip_block_between_markers_inplace(text, "```bash", "```");
    strip_block_between_markers_inplace(text, "```shell", "```");
    strip_block_between_markers_inplace(text, "```json", "```");
    strip_inline_transcript_suffix_inplace(text);
    trim_trailing_ascii_space(text);
}

void tool_delegate_sanitize_summary_text_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }
    strscpy(dst, src, dst_size);
    tool_delegate_sanitize_summary_text_inplace(dst);
}

bool tool_delegate_safe_text_is_directly_usable(const char *text)
{
    if (!text || !text[0]) {
        return false;
    }
    if (strncmp(text, "delegate_task:", strlen("delegate_task:")) == 0) {
        return false;
    }
    if (tool_delegate_text_has_dsml_markup(text) ||
        tool_delegate_text_has_transcript_markup(text)) {
        return false;
    }
    return true;
}
