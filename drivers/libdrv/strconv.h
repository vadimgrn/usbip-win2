/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"

namespace libdrv
{

constexpr auto isdigit(wchar_t ch)
{
        return ch <= L'9' && ch >= L'0';
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS unicode_to_utf8(_Inout_ UTF8_STRING &dst, _In_ const UNICODE_STRING &src);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS unicode_to_utf8(_Out_writes_bytes_opt_(maxlen) char *utf8, _In_ USHORT maxlen, _In_ const UNICODE_STRING &src);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS utf8_to_unicode(
        _Out_ UNICODE_STRING &dst, _In_ const UTF8_STRING &src, _In_ POOL_TYPE pooltype, _In_ ULONG pooltag);

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
 * @param ch character to search
 * @return position of the first found character or -1
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED int strchr(_In_ const UNICODE_STRING &s, _In_ WCHAR ch);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool equal_strings(
        _In_reads_bytes_(max_a) const char *a, _In_ size_t max_a,
        _In_reads_bytes_(max_b) const char *b, _In_ size_t max_b);

template<size_t N1, size_t N2>
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline bool equal_strings(
        _In_reads_bytes_(N1) const char (&a)[N1],
        _In_reads_bytes_(N2) const char (&b)[N2])
{
        return equal_strings(a, sizeof(a), b, sizeof(b));
}

} // namespace libdrv
