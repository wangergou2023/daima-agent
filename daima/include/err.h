/* 错误码定义。 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t daima_err_t;

enum {
    DAIMA_OK = 0,
    DAIMA_FAIL = -1,
    DAIMA_ERR_NO_MEM = -2,
    DAIMA_ERR_INVALID_ARG = -3,
    DAIMA_ERR_INVALID_STATE = -4,
    DAIMA_ERR_INVALID_SIZE = -5,
    DAIMA_ERR_TIMEOUT = -6,
    DAIMA_ERR_NOT_FOUND = -7,
    DAIMA_ERR_HTTP_CONNECT = -8,
    DAIMA_ERR_HTTP_WRITE_DATA = -9,
    DAIMA_ERR_HTTP_FETCH_HEADER = -10,
    DAIMA_ERR_NVS_NOT_FOUND = -11,
    DAIMA_ERR_NVS_NO_FREE_PAGES = -12,
    DAIMA_ERR_NVS_NEW_VERSION_FOUND = -13,
};

const char *daima_err_to_name(daima_err_t err);

#define DAIMA_ERROR_CHECK(x)                                                       \
    do {                                                                           \
        daima_err_t __err = (x);                                                    \
        if (__err != DAIMA_OK) {                                                   \
            fprintf(stderr, "DAIMA_ERROR_CHECK failed: %s (%d) at %s:%d\n",         \
                    daima_err_to_name(__err), (int)__err, __FILE__, __LINE__);      \
            abort();                                                               \
        }                                                                          \
    } while (0)

#ifdef __cplusplus
}
#endif
