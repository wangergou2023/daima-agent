/* HR 主管线：scan → cluster → distill → register。 */
#pragma once

#include "err.h"
#include <stdbool.h>

err_t hr_run_pipeline(bool auto_register, int *out_registered_count);
