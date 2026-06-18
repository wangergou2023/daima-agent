/* 技能元数据接口。
 * 提供技能名称校验、路径解析、SKILL.md 元数据（标题+描述）读取功能。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/* 技能元数据 */
typedef struct {
    char title[128];        /* 技能标题（来自 YAML name 或 Markdown # 标题） */
    char description[256];  /* 技能描述（来自 YAML description 或首段正文） */
} skill_meta_t;

/* 校验技能名称是否为安全相对路径 */
bool skill_meta_validate_name(const char *name);

/**
 * 将技能名称解析为文件路径。
 * @param name          技能名
 * @param file_path     文件名（NULL 默认 SKILL.md）
 * @param resolved      输出缓冲区
 * @param resolved_size 缓冲区大小
 */
bool skill_meta_resolve_path(const char *name,
                             const char *file_path,
                             char *resolved,
                             size_t resolved_size);

/* 解析技能文件的标题和描述元数据 */
bool skill_meta_read_file(const char *path, skill_meta_t *meta);
