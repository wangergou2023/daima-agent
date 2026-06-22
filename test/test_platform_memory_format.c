#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "drivers/platform/platform.h"

int main(void)
{
    char buf[64];
    size_t large = (size_t)INT_MAX + 4096u;

    assert(platform_format_bytes(0, buf, sizeof(buf)));
    assert(strcmp(buf, "0") == 0);

    assert(platform_format_bytes(large, buf, sizeof(buf)));
    assert(strcmp(buf, "2147487743") == 0);
    assert(buf[0] != '-');

    printf("platform memory format tests passed\n");
    return 0;
}
