/* context_build 文件片段与记忆章节拼装。 */

#include "context_sections.h"

#include "drivers/memory/memory_store.h"

#include <stdarg.h>
#include <stdio.h>

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

size_t context_sections_append_file(char *buf, size_t size, size_t offset,
				      const char *path, const char *header)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		return offset;
	}

	if (header && offset < size - 1) {
		offset = append_textf(buf, size, offset, "\n## %s\n\n", header);
	}

	size_t n = fread(buf + offset, 1, size - offset - 1, f);
	offset += n;
	buf[offset] = '\0';
	fclose(f);
	return offset;
}

size_t context_sections_append_identity(char *buf, size_t size, size_t offset,
					 const guide_paths_t *guide_paths)
{
	if (!guide_paths) {
		return offset;
	}

	offset = context_sections_append_file(buf, size, offset, guide_paths->identity_path,
						      "身份设定");
	offset = context_sections_append_file(buf, size, offset, guide_paths->soul_path,
						      "个性设定");
	return context_sections_append_file(buf, size, offset, guide_paths->user_path,
					      "用户信息");
}

size_t context_sections_append_memory(char *buf, size_t size, size_t offset)
{
	char mem_buf[4096];
	char recent_buf[4096];

	if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == 0 && mem_buf[0]) {
		offset = append_textf(buf, size, offset, "\n## 长期记忆\n\n%s\n", mem_buf);
	}

	if (memory_read_recent(recent_buf, sizeof(recent_buf), 3) == 0 && recent_buf[0]) {
		offset = append_textf(buf, size, offset, "\n## 最近笔记\n\n%s\n", recent_buf);
	}

	return offset;
}
