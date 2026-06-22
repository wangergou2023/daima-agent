/* 技能摘要构建接口。 */

#pragma once

#include <stddef.h>

void skill_summary_init(void);
size_t skill_summary_build_for_channel(const char *channel, char *buf, size_t size);
