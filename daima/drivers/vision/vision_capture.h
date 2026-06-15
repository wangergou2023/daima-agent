/* 图像抓拍（JPEG）。
 * - MIPS 平台使用 IMP 接口
 * - Host 平台为占位实现
 */

#pragma once

#include "err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化图像抓拍模块（MIPS/IMP）。
 * 可重复调用，内部会做幂等保护。
 */
err_t vision_capture_init(void);

/**
 * 抓拍一张 JPEG 并保存到文件。
 * - output_path 当前未使用（使用 sample 默认路径）。
 * - out_path/out_path_len 可选，用于返回实际保存路径。
 * - 失败返回 DAIMA_ERR_*，成功返回 DAIMA_OK。
 */
err_t vision_capture_jpeg(const char *output_path,
                               char *out_path,
                               size_t out_path_len);

/**
 * 反初始化图像抓拍模块。
 * 释放 IMP 资源、解绑通道。
 */
void vision_capture_shutdown(void);

#ifdef __cplusplus
}
#endif
