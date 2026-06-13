/* 通道运行时分发：统一封装普通出站文本。 */

#pragma once

#include "core/bus.h"
#include "core/err.h"

daima_err_t channel_runtime_dispatch_outbound(const daima_msg_t *msg);
