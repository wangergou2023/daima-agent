/* 语音唤醒按键与录音处理（MIPS 平台）。 */

#pragma once

#include "core/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 启动按键监听与录音处理线程（仅 BUILD_FOR_MIPS 可用）。
 */
daima_err_t voice_wake_start(void);

/**
 * 停止按键监听线程（仅 BUILD_FOR_MIPS 可用）。
 */
void voice_wake_stop(void);

#ifdef __cplusplus
}
#endif
