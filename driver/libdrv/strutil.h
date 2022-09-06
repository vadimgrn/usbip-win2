#pragma once

#include <wdm.h>

LPSTR libdrv_strdup(POOL_FLAGS Flags, LPCSTR str);
LPWSTR libdrv_strdup(POOL_FLAGS Flags, LPCWSTR str);
void libdrv_free(void *data);
