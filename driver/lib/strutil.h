#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <wdm.h>

LPWSTR libdrv_strdupW(POOL_TYPE PoolType, LPCWSTR str);
void libdrv_free(void *data);

#ifdef __cplusplus
}
#endif