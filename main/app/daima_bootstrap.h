#pragma once

#include <stdbool.h>
#include <stddef.h>

void daima_bootstrap_print_usage(const char *prog);
void daima_bootstrap_prepare_runtime(void);
bool daima_bootstrap_get_primary_ipv4(char *out, size_t out_sz);
