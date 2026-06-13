/* tts_player — 语音播放管线: 文本 → TTS → 音频处理 → PlayPCM */
#ifndef DAIMA_TTS_PLAYER_H
#define DAIMA_TTS_PLAYER_H

#include "core/err.h"

daima_err_t tts_player_speak(const char *text);

#endif
