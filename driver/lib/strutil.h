#pragma once

#include <ntddk.h>

LPWSTR libdrv_strdupW(LPCWSTR cwstr);
void libdrv_free(void *data);

int libdrv_snprintf(char *buf, int size, const char *fmt, ...);
int libdrv_snprintfW(PWCHAR buf, int size, LPCWSTR fmt, ...);

