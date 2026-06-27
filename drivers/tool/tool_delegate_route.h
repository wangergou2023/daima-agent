/* delegate_task routing helpers */
#pragma once

#include <stddef.h>

#include "err.h"

err_t tool_delegate_execute(const char *input_json,
                            char *output,
                            size_t output_size);
