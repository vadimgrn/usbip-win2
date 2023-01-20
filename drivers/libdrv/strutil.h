/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"

namespace libdrv
{

LPSTR strdup(POOL_FLAGS Flags, LPCSTR str);
LPWSTR strdup(POOL_FLAGS Flags, LPCWSTR str);
void free(void *data);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS to_unicode_str(_Out_ UNICODE_STRING &dst, _In_ const char *ansi);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS to_ansi_str(_Out_ char *dest, _In_ USHORT len, _In_ const UNICODE_STRING &src);

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED USHORT strrchr(_In_ const UNICODE_STRING &s, _In_ WCHAR ch);

} // namespace libdrv