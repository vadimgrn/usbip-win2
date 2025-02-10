/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"

namespace libdrv
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS unicode_to_utf8(_Inout_ UTF8_STRING &dst, _In_ const UNICODE_STRING &src);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS unicode_to_utf8(_Out_opt_ char *utf8, _In_ USHORT maxlen, _In_ const UNICODE_STRING &src);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS utf8_to_unicode(
        _Inout_ UNICODE_STRING &dst, _In_ const UTF8_STRING &src, _In_ POOL_TYPE pooltype, _In_ ULONG pooltag);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS utf8_to_unicode(
        _Out_ UNICODE_STRING &dst, _In_ const char *utf8, _In_ USHORT maxlen, 
        _In_ POOL_TYPE pooltype, _In_ ULONG pooltag);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void FreeUnicodeString(_Inout_ UNICODE_STRING &s, _In_ ULONG pooltag);

constexpr auto empty(_In_ const UNICODE_STRING &s) { return !s.Length; }

/**
 * head and str OR tail and str can be the same object.
 *
 * @param head substring before the separator
 * @param tail rest of the string after the separator
 * @param str string to split
 * @param sep separator character
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void split(
        _Out_ UNICODE_STRING &head, _Out_ UNICODE_STRING &tail, 
        _In_ const UNICODE_STRING &str, _In_ WCHAR sep);

/**
 * @param s string to search
 * @param ch character to search in a string
 * @param from position to start a search
 * @return position of the first found character in the string or -1 if not found 
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED int strchr(_In_ const UNICODE_STRING &s, _In_ WCHAR ch, _In_ int from = 0);

} // namespace libdrv
