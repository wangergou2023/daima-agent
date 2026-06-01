/* 语音唤醒按键与录音处理（Host 占位实现）。 */

#include "voice/voice_wake.h"
#include "daima_log.h"

static const char *TAG = "voice_wake";

daima_err_t voice_wake_start(void)
{
    DAIMA_LOGW(TAG, "voice_wake_start not implemented");
    return DAIMA_ERR_INVALID_STATE;
}

void voice_wake_stop(void)
{
    DAIMA_LOGW(TAG, "voice_wake_stop not implemented");
}
