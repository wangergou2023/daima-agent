/* 技能加载接口。 */

#pragma once

#include "err.h"
#include <stddef.h>

/**
 * 初始化技能系统。
 * 若内置技能文件不存在，则写入到 SPIFFS。
 */
daima_err_t skill_loader_init(void);

/**
 * 为系统提示构建所有可用技能的摘要。
 * 列出每个技能的标题和描述。
 *
 * @param buf   输出缓冲区
 * @param size  缓冲区大小
 * @return 写入字节数（若未找到技能则为 0）
 */
size_t skill_loader_build_summary(char *buf, size_t size);

/**
 * 为指定 channel 构建技能摘要。
 * - 总是包含根目录通用技能：skills/<name>/SKILL.md
 * - 仅包含当前通道技能：skills/channels/<channel>/<name>/SKILL.md
 */
size_t skill_loader_build_summary_for_channel(const char *channel, char *buf, size_t size);
