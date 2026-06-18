/* 启动引导：运行时准备、IP 获取。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/** 启动前运行时准备：路径初始化 → 目录创建 → 配置加载。 */
void bootstrap_prepare_runtime(void);

/**
 * 获取本机首选 IPv4 地址。
 * @param out    输出缓冲区
 * @param out_sz 缓冲区大小
 * @return 成功返回 true，失败返回 false
 */
bool bootstrap_get_primary_ipv4(char *out, size_t out_sz);
