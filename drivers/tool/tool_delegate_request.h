/* delegate_task request parsing helpers */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "err.h"
#include "drivers/tool/tool_delegate_types.h"

err_t tool_delegate_parse_request(const char *input_json,
                                  delegate_request_t *req,
                                  char *output,
                                  size_t output_size);
