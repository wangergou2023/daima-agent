/* 引导文件路径解析与相关文案拼装。 */

#include "guide_paths.h"

#include "paths.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

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

void guide_paths_init(guide_paths_t *paths)
{
	if (!paths) {
		return;
	}

	snprintf(paths->bootstrap_path, sizeof(paths->bootstrap_path), "%s/BOOTSTRAP.md",
		 path_config_dir());
	snprintf(paths->identity_path, sizeof(paths->identity_path), "%s/IDENTITY.md",
		 path_config_dir());
	snprintf(paths->soul_path, sizeof(paths->soul_path), "%s/SOUL.md", path_config_dir());
	snprintf(paths->user_path, sizeof(paths->user_path), "%s/USER.md", path_config_dir());
}

size_t guide_paths_append_runtime_guide(char *buf, size_t size, size_t offset,
					 const guide_paths_t *paths)
{
	if (!paths) {
		return offset;
	}

	offset = append_textf(
		buf, size, offset,
		"\n## 记忆与引导文件\n\n"
		"### 持久化记忆\n"
		"- 长期记忆：`%s`\n"
		"- 每日笔记：`%s/<YYYY-MM-DD>.md`\n"
		"- 更新记忆前先用 `files action=read`，再用 `apply_patch` 做最小改动；写每日笔记前先 `get_current_time`。\n\n"
		"### 可读取与按需更新的引导文件\n"
		"- Bootstrap：`%s`\n"
		"- Identity：`%s`\n"
		"- Personality：`%s`\n"
		"- User Info：`%s`\n"
		"- 更新这些文件时，先用 `files action=search/read` 看上下文，再用 `apply_patch` 做最小改动，避免直接覆盖。\n"
		"- 若文件不存在，用 `apply_patch` 的 `*** Add File` 创建。\n",
		path_memory_dir(),
		path_memory_dir(),
		paths->bootstrap_path,
		paths->identity_path,
		paths->soul_path,
		paths->user_path);

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
