/* 技能摘要构建与缓存。 */

#include "drivers/skill/skill_summary.h"
#include "drivers/skill/skill_meta.h"
#include "autoconf.h"
#if SKILL_SCOPED_TOOLS_ENABLED
#include "drivers/skill/skill_tools.h"
#endif
#include "paths.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include "linux/printk.h"
#include "linux/kernel.h"

#define SKILL_SUMMARY_CACHE_TTL_SEC 5
#define SKILL_SUMMARY_CACHE_MAX     8
#define SKILL_SUMMARY_CHANNEL_LEN   32
#define SKILL_SUMMARY_BUF_SIZE      (16 * 1024)

typedef struct {
    bool valid;
    char channel[SKILL_SUMMARY_CHANNEL_LEN];
    time_t built_at;
    size_t len;
    char summary[SKILL_SUMMARY_BUF_SIZE];
} skill_summary_cache_entry_t;

static skill_summary_cache_entry_t s_summary_cache[SKILL_SUMMARY_CACHE_MAX];

static bool has_suffix(const char *str, const char *suffix)
{
    if (!str || !suffix) return false;
    size_t len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (suf_len == 0 || len < suf_len) return false;
    return strcmp(str + len - suf_len, suffix) == 0;
}

static bool is_dir_path(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static bool append_skill_summary_from_file(char *buf, size_t size, size_t *off, const char *full_path)
{
    if (!buf || !off || !full_path || size == 0 || *off >= size - 1) return false;
    skill_meta_t meta = {0};
    if (!skill_meta_read_file(full_path, &meta)) return false;

    *off += snprintf(buf + *off, size - *off,
        "- **%s**: %s (read with: files {\"action\":\"read\",\"path\":\"%s\"})\n",
        meta.title[0] ? meta.title : "(untitled)",
        meta.description[0] ? meta.description : "(no description)",
        full_path);
    return true;
}

static bool channel_name_is_safe(const char *channel)
{
    if (!channel || !channel[0]) return false;
    for (const char *p = channel; *p; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') {
            return false;
        }
    }
    return true;
}

static bool append_skill_summary_for_entry(char *buf,
                                           size_t size,
                                           size_t *off,
                                           const char *skill_dir,
                                           const char *entry_name,
                                           bool *found)
{
    if (!entry_name || entry_name[0] == '.') return false;

    char entry_path[296];
    snprintf(entry_path, sizeof(entry_path), "%s/%s", skill_dir, entry_name);

    if (is_dir_path(entry_path)) {
        char full_path[320];
        snprintf(full_path, sizeof(full_path), "%s/SKILL.md", entry_path);

        if (append_skill_summary_from_file(buf, size, off, full_path)) {
#if SKILL_SCOPED_TOOLS_ENABLED
            skill_tools_register(entry_name, entry_path);
#endif
            *found = true;
            return true;
        }
        return false;
    }

    size_t name_len = strlen(entry_name);
    if (name_len < 4 || !has_suffix(entry_name, ".md")) {
        return false;
    }

    if (append_skill_summary_from_file(buf, size, off, entry_path)) {
        *found = true;
        return true;
    }
    return false;
}

static void append_skill_dir_summaries(char *buf, size_t size, size_t *off, const char *skill_dir, bool *found)
{
    DIR *dir = opendir(skill_dir);
    if (!dir) {
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL && *off < size - 1) {
        append_skill_summary_for_entry(buf, size, off, skill_dir, ent->d_name, found);
    }
    closedir(dir);
}

static const char *cache_channel_key(const char *channel)
{
    return channel && channel[0] ? channel : "";
}

static skill_summary_cache_entry_t *find_summary_cache_entry(const char *channel_key)
{
    for (int i = 0; i < SKILL_SUMMARY_CACHE_MAX; ++i) {
        if (s_summary_cache[i].valid && strcmp(s_summary_cache[i].channel, channel_key) == 0) {
            return &s_summary_cache[i];
        }
    }
    return NULL;
}

static skill_summary_cache_entry_t *alloc_summary_cache_entry(const char *channel_key)
{
    int oldest = 0;
    for (int i = 0; i < SKILL_SUMMARY_CACHE_MAX; ++i) {
        if (!s_summary_cache[i].valid) {
            oldest = i;
            break;
        }
        if (s_summary_cache[i].built_at < s_summary_cache[oldest].built_at) {
            oldest = i;
        }
    }
    skill_summary_cache_entry_t *entry = &s_summary_cache[oldest];
    memset(entry, 0, sizeof(*entry));
    entry->valid = true;
    strscpy(entry->channel, channel_key, sizeof(entry->channel));
    return entry;
}

static size_t copy_summary_to_output(const char *summary, size_t summary_len, char *buf, size_t size)
{
    if (!buf || size == 0) {
        return 0;
    }
    size_t copy = summary_len;
    if (copy >= size) {
        copy = size - 1;
    }
    if (copy > 0 && summary) {
        memcpy(buf, summary, copy);
    }
    buf[copy] = '\0';
    return copy;
}

static size_t skill_summary_build_uncached(const char *channel, char *buf, size_t size)
{
    size_t off = 0;
    bool found = false;
    buf[0] = '\0';

    DIR *dir = opendir(path_spiffs_base());
    if (!dir) {
        pr_warn("Cannot open SPIFFS for skill enumeration");
    }

    struct dirent *ent;
    const char *skills_subdir = "drivers/skill/";
    const size_t subdir_len = strlen(skills_subdir);

    if (dir) {
        while ((ent = readdir(dir)) != NULL && off < size - 1) {
            const char *name = ent->d_name;
            if (strncmp(name, skills_subdir, subdir_len) != 0) continue;

            size_t name_len = strlen(name);
            if (name_len < subdir_len + 4) continue;

            const char *subpath = name + subdir_len;
            bool has_subdir = strchr(subpath, '/') != NULL;
            bool is_new_layout = has_suffix(name, "/SKILL.md");
            bool is_legacy_layout = !has_subdir && has_suffix(name, ".md");
            if (!is_new_layout && !is_legacy_layout) continue;

            char full_path[296];
            snprintf(full_path, sizeof(full_path), "%s/%s", path_spiffs_base(), name);

            if (append_skill_summary_from_file(buf, size, &off, full_path)) {
#if SKILL_SCOPED_TOOLS_ENABLED
                char skill_name[128];
                snprintf(skill_name, sizeof(skill_name), "%.*s", (int)(strlen(subpath) - strlen("/SKILL.md")), subpath);
                char skill_dir[320];
                snprintf(skill_dir, sizeof(skill_dir), "%.*s", (int)(strlen(full_path) - strlen("/SKILL.md")), full_path);
                skill_tools_register(skill_name, skill_dir);
#endif
                found = true;
            }
        }
        closedir(dir);
    }

    if (!found) {
        DIR *skills_dir = opendir(path_skills_dir());
        if (!skills_dir) {
            pr_warn("Cannot open skills directory for enumeration");
        } else {
            while ((ent = readdir(skills_dir)) != NULL && off < size - 1) {
                if (strcmp(ent->d_name, "channels") == 0) continue;
                append_skill_summary_for_entry(buf, size, &off, path_skills_dir(), ent->d_name, &found);
            }
            closedir(skills_dir);
        }
    }

    if (channel_name_is_safe(channel) && off < size - 1) {
        char channel_dir[320];
        snprintf(channel_dir, sizeof(channel_dir), "%s/channels/%s", path_skills_dir(), channel);
        append_skill_dir_summaries(buf, size, &off, channel_dir, &found);
    }

    buf[off] = '\0';
    pr_debug("Skills summary: %d bytes", (int)off);
    return off;
}

void skill_summary_init(void)
{
    memset(s_summary_cache, 0, sizeof(s_summary_cache));
}

size_t skill_summary_build_for_channel(const char *channel, char *buf, size_t size)
{
    if (!buf || size == 0) {
        return 0;
    }

    const char *key = cache_channel_key(channel);
    time_t now = time(NULL);
    skill_summary_cache_entry_t *entry = find_summary_cache_entry(key);
    if (entry && now - entry->built_at <= SKILL_SUMMARY_CACHE_TTL_SEC) {
        pr_debug("Skills summary cache hit: channel=%s bytes=%d", key[0] ? key : "(common)", (int)entry->len);
        return copy_summary_to_output(entry->summary, entry->len, buf, size);
    }

    char summary[SKILL_SUMMARY_BUF_SIZE];
    size_t len = skill_summary_build_uncached(channel, summary, sizeof(summary));
    entry = entry ? entry : alloc_summary_cache_entry(key);
    entry->built_at = now;
    entry->len = len;
    copy_summary_to_output(summary, len, entry->summary, sizeof(entry->summary));

    return copy_summary_to_output(entry->summary, entry->len, buf, size);
}
