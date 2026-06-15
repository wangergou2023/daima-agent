/* 错误码定义。 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t err_t;

enum {
    ERR_FAIL = -1,
    ERR_NO_MEM = -2,
    ERR_INVALID_ARG = -3,
    ERR_INVALID_STATE = -4,
    ERR_INVALID_SIZE = -5,
    ERR_TIMEOUT = -6,
    ERR_NOT_FOUND = -7,
    ERR_HTTP_CONNECT = -8,
    ERR_HTTP_WRITE_DATA = -9,
    ERR_HTTP_FETCH_HEADER = -10,
    ERR_NVS_NOT_FOUND = -11,
    ERR_NVS_NO_FREE_PAGES = -12,
    ERR_NVS_NEW_VERSION_FOUND = -13,
};

const char *err_name(err_t err);

#define ERR_CHECK(x)                                                       \
    do {                                                                           \
        err_t __err = (x);                                                    \
        if (__err != 0) {                                                   \
            fprintf(stderr, "ERR_CHECK failed: %s (%d) at %s:%d\n",         \
                    err_name(__err), (int)__err, __FILE__, __LINE__);      \
            abort();                                                               \
        }                                                                          \
    } while (0)

#ifdef __cplusplus
}
#endif
