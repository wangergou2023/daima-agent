#include "drivers/voice/voice_channel.h"
#include <errno.h>

int voice_channel_handle_audio(const char *chat_id,
                                       const unsigned char *audio_bytes,
                                       size_t audio_len,
                                       const char *asr_model,
                                       const char *prompt,
                                       const char *hotwords_json,
                                       const char *request_id,
                                       const char *user_id,
                                       const char *voice,
                                       const char *response_format)
{
    (void)chat_id;
    (void)audio_bytes;
    (void)audio_len;
    (void)asr_model;
    (void)prompt;
    (void)hotwords_json;
    (void)request_id;
    (void)user_id;
    (void)voice;
    (void)response_format;
    return -ENODEV;
}
