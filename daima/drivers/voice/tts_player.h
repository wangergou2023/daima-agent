/* tts_player — 语音播放管线: 文本 → TTS → 音频处理 → PlayPCM */
#ifndef TTS_PLAYER_H
#define TTS_PLAYER_H

#include "err.h"

err_t tts_player_speak(const char *text);

#endif
