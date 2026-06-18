/* 技能元数据管理——技能文件的名称校验、路径解析与元数据解析。
 * 元数据来源：YAML front matter（name / description）和 Markdown 标题。 */

#include "drivers/skill/skill_meta.h"

#include "paths.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* 检查字符串是否包含 ".."（路径穿越防护）。 */
static bool contains_dotdot(const char *s)
{
    return s && strstr(s, "..") != NULL;
}

/* 校验相对路径安全性：非空、非绝对路径、无 ".."、仅含字母数字/-/_/ / 。 */
static bool is_safe_relative_path(const char *s)
{
    if (!s || !s[0] || s[0] == '/' || contains_dotdot(s)) {
        return false;
    }
    for (const char *p = s; *p; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '/') {
            return false;
        }
    }
    return true;
}

/* 判断行是否为 YAML front matter 分隔符 "---"。 */
static bool is_front_matter_delim(const char *line)
{
    if (!line) return false;
    while (*line && isspace((unsigned char)*line)) line++;
    if (line[0] != '-' || line[1] != '-' || line[2] != '-') return false;
    line += 3;
    while (*line && isspace((unsigned char)*line)) line++;
    return *line == '\0' || *line == '\n' || *line == '\r';
}

/**
 * 解析 YAML 中的单行键值对（如 "name: 技能名"）。
 * 支持双引号/单引号包裹的值，自动去除首尾空格和引号。
 * @param line     YAML 行
 * @param key      期望的键名
 * @param out      值输出缓冲区
 * @param out_size 缓冲区大小
 * @return 解析成功返回 true
 */
static bool parse_yaml_value(const char *line, const char *key, char *out, size_t out_size)
{
    if (!line || !key || !out || out_size == 0) return false;

    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t key_len = strlen(key);
    if (strncmp(p, key, key_len) != 0) return false;
    p += key_len;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return false;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;

    size_t len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) {
        len--;
    }
    if (len == 0) return false;
    if ((p[0] == '"' || p[0] == '\'') && len > 1 && p[len - 1] == p[0]) {
        p++;
        len -= 2;
    }

    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, p, copy);
    out[copy] = '\0';
    return true;
}

/* 从 "# 标题" 格式行中提取标题文本（去除 "# " 前缀和尾部空白）。 */
static const char *extract_title(const char *line, size_t len, char *out, size_t out_size)
{
    const char *start = line;
    if (len >= 2 && line[0] == '#' && line[1] == ' ') {
        start = line + 2;
        len -= 2;
    }

    while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r' || start[len - 1] == ' ')) {
        len--;
    }

    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, start, copy);
    out[copy] = '\0';
    return out;
}

/* 判断是否到达描述文本的终止位置：空行或 "##" 标题行。 */
static bool is_desc_terminator(const char *line, size_t len)
{
    if (len == 0) return true;
    if (len == 1 && line[0] == '\n') return true;
    if (len >= 2 && line[0] == '#' && line[1] == '#') return true;
    return false;
}

/* 从文件当前位置解析 YAML front matter，提取 name 和 description。 */
static bool parse_front_matter(FILE *f, char *title, size_t title_sz, char *desc, size_t desc_sz)
{
    bool got_title = false;
    bool got_desc = false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (is_front_matter_delim(line)) break;
        if (!got_title) got_title = parse_yaml_value(line, "name", title, title_sz);
        if (!got_desc) got_desc = parse_yaml_value(line, "description", desc, desc_sz);
    }
    return got_title || got_desc;
}

/* 将一行文本追加到描述缓冲区，用空格分隔。跳过仅含空白符的行。 */
static void append_desc_line(const char *line, size_t len, char *out, size_t out_size, size_t *off)
{
    if (!line || !out || !off || out_size == 0) return;
    size_t trimmed = len;
    while (trimmed > 0 && (line[trimmed - 1] == '\n' || line[trimmed - 1] == '\r')) {
        trimmed--;
    }
    if (trimmed == 0) return;
    if (*off == 0) {
        size_t i = 0;
        while (i < trimmed && isspace((unsigned char)line[i])) i++;
        if (i == trimmed) return;
    }
    size_t copy = trimmed < out_size - *off - 1 ? trimmed : out_size - *off - 1;
    memcpy(out + *off, line, copy);
    *off += copy;
    if (*off < out_size - 1) {
        out[(*off)++] = ' ';
    }
}

/* 从文件读取描述文本，从指定首行开始，遇到 "##" 或空行停止。 */
static void extract_description_with_first(FILE *f,
                                           const char *first_line,
                                           bool include_first,
                                           char *out,
                                           size_t out_size)
{
    size_t off = 0;
    if (include_first && first_line) {
        size_t len = strlen(first_line);
        if (!is_desc_terminator(first_line, len)) {
            append_desc_line(first_line, len, out, out_size, &off);
        }
    }

    char line[256];
    while (fgets(line, sizeof(line), f) && off < out_size - 1) {
        size_t len = strlen(line);
        if (is_desc_terminator(line, len)) break;
        append_desc_line(line, len, out, out_size, &off);
    }

    while (off > 0 && out[off - 1] == ' ') off--;
    out[off] = '\0';
}

/* 校验技能名称是否为安全相对路径。 */
bool skill_meta_validate_name(const char *name)
{
    return is_safe_relative_path(name);
}

/**
 * 将技能名称解析为文件路径。
 * @param name          技能名（相对路径）
 * @param file_path     文件路径（NULL 则默认 SKILL.md）
 * @param resolved      输出：解析后的完整路径
 * @param resolved_size 缓冲区大小
 * @return 解析成功返回 true
 */
bool skill_meta_resolve_path(const char *name,
                             const char *file_path,
                             char *resolved,
                             size_t resolved_size)
{
    if (!skill_meta_validate_name(name) || !resolved || resolved_size == 0) {
        return false;
    }

    if (!file_path || !file_path[0]) {
        return snprintf(resolved, resolved_size, "%s/%s/SKILL.md", path_skills_dir(), name) < resolved_size;
    }

    if (file_path[0] == '/' || contains_dotdot(file_path)) {
        return false;
    }

    return snprintf(resolved, resolved_size, "%s/%s/%s", path_skills_dir(), name, file_path) < resolved_size;
}

/**
 * 解析技能文件元数据（标题和描述）。
 * 优先从 YAML front matter 读取，不足时从 Markdown 正文补充。
 * @param path 技能文件完整路径
 * @param meta 输出的元数据结构体
 * @return 至少解析到一项元数据时返回 true
 */
bool skill_meta_read_file(const char *path, skill_meta_t *meta)
{
    if (!path || !meta) {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }

    memset(meta, 0, sizeof(*meta));

    char first_line[256];
    if (!fgets(first_line, sizeof(first_line), f)) {
        fclose(f);
        return false;
    }

    if (is_front_matter_delim(first_line)) {
        parse_front_matter(f, meta->title, sizeof(meta->title), meta->description, sizeof(meta->description));

        bool need_title = meta->title[0] == '\0';
        bool need_desc = meta->description[0] == '\0';
        if (need_title || need_desc) {
            char content_first[256];
            if (fgets(content_first, sizeof(content_first), f)) {
                bool first_is_title = (content_first[0] == '#');
                if (need_title) {
                    extract_title(content_first, strlen(content_first), meta->title, sizeof(meta->title));
                }
                if (need_desc) {
                    extract_description_with_first(f,
                                                   content_first,
                                                   !first_is_title,
                                                   meta->description,
                                                   sizeof(meta->description));
                }
            }
        }
    } else {
        extract_title(first_line, strlen(first_line), meta->title, sizeof(meta->title));
        extract_description_with_first(f, NULL, false, meta->description, sizeof(meta->description));
    }

    fclose(f);
    return meta->title[0] || meta->description[0];
}
