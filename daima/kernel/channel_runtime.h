/* 通道运行时分发：统一封装普通出站文本。 */

#pragma once

#include "bus.h"
#include "err.h"

daima_err_t channel_runtime_dispatch_outbound(const struct message *msg);
