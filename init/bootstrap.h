#pragma once

#include <stdbool.h>
#include <stddef.h>

void bootstrap_print_usage(const char *prog);
void bootstrap_prepare_runtime(void);
bool bootstrap_get_primary_ipv4(char *out, size_t out_sz);
