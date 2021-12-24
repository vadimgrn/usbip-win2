#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <ntdef.h>

LPWSTR libdrv_strdupW(LPCWSTR str);
void libdrv_free(void *data);

#ifdef __cplusplus
}
#endif