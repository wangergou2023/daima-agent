/* context_build 章节拼装辅助。 */

#pragma once

#include "guide_paths.h"

#include <stddef.h>

size_t context_sections_append_file(char *buf, size_t size, size_t offset,
				      const char *path, const char *header);

size_t context_sections_append_identity(char *buf, size_t size, size_t offset,
					 const guide_paths_t *guide_paths);

size_t context_sections_append_memory(char *buf, size_t size, size_t offset);
