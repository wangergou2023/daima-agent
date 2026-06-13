/* 语音唤醒按键与录音处理（Host 占位实现）。 */

#include "drivers/voice/voice_wake.h"
#include "linux/printk.h"
daima_err_t voice_wake_start(void)
{
    pr_warn("voice_wake_start not implemented");
    return DAIMA_ERR_INVALID_STATE;
}

void voice_wake_stop(void)
{
    pr_warn("voice_wake_stop not implemented");
}
