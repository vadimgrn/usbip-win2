#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <wdm.h>

LPWSTR libdrv_strdup(POOL_FLAGS Flags, LPCWSTR str);
void libdrv_free(void *data);

#ifdef __cplusplus
}
#endif