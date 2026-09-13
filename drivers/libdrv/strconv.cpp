/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "strconv.h"
#include <ntstrsafe.h>

/*
 * @see RtlUnicodeStringToUTF8String since Windows 10, version 2004
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::unicode_to_utf8(_Inout_ UTF8_STRING &dst, _In_ const UNICODE_STRING &src)
{
        PAGED_CODE();

        ULONG actual{};
        auto st = RtlUnicodeToUTF8N(dst.Buffer, dst.MaximumLength, &actual, src.Buffer, src.Length);

        if (actual > UNICODE_STRING_MAX_BYTES) [[unlikely]] {
                dst.Length = 0;
                return STATUS_BUFFER_TOO_SMALL;
        }

        dst.Length = static_cast<USHORT>(actual);
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::unicode_to_utf8(_Out_writes_bytes_opt_(maxlen) char *utf8, _In_ USHORT maxlen, _In_ const UNICODE_STRING &src)
{
        PAGED_CODE();

        UTF8_STRING s {
                .MaximumLength = static_cast<USHORT>(maxlen ? maxlen - 1 : 0),
                .Buffer = utf8
        };

        auto st = unicode_to_utf8(s, src);

        if (s.Buffer && maxlen) {
                s.Buffer[s.Length] = '\0';
        }

        return st;
}

/** 
 * @return libdrv::FreeUnicodeString must be used to release a memory
 * @see RtlUTF8StringToUnicodeString since Windows 10, version 2004
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::utf8_to_unicode(
        _Out_ UNICODE_STRING &dst, _In_ const UTF8_STRING &src, _In_ POOL_TYPE pooltype, _In_ ULONG pooltag)
{
        PAGED_CODE();

        if (dst.Buffer) {
                return STATUS_ALREADY_INITIALIZED;
        }

        ULONG actual;
        if (auto st = RtlUTF8ToUnicodeN(nullptr, 0, &actual, src.Buffer, src.Length); NT_ERROR(st)) {
                return st;
        }

        if (actual > UNICODE_STRING_MAX_BYTES) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        dst.Buffer = (WCHAR*)ExAllocatePoolUninitialized(pooltype, actual, pooltag);
        if (!dst.Buffer) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        dst.MaximumLength = static_cast<USHORT>(actual);
        auto st = RtlUTF8ToUnicodeN(dst.Buffer, dst.MaximumLength, &actual, src.Buffer, src.Length);

        if (bool err = NT_ERROR(st); err || actual > UNICODE_STRING_MAX_BYTES) {
                FreeUnicodeString(dst, pooltag);
                return err ? st : STATUS_BUFFER_TOO_SMALL;
        }

        dst.Length = static_cast<USHORT>(actual);
        return st;
}

/*
 * @param src null-terminated utf8 string
 * @param maxlen maximum size in bytes of the buffer to which src points (this is not a length/size of str itself)
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::utf8_to_unicode(
        _Out_ UNICODE_STRING &dst, _In_ const char *utf8, _In_ USHORT maxlen, 
        _In_ POOL_TYPE pooltype, _In_ ULONG pooltag)
{
        PAGED_CODE();

        size_t cb;
        if (auto err = RtlStringCbLengthA(utf8, maxlen, &cb)) { 
                return err;
        }

        UTF8_STRING u8 {
                .Length = static_cast<USHORT>(cb),
                .MaximumLength = maxlen, 
                .Buffer = const_cast<char*>(utf8)
        };

        return utf8_to_unicode(dst, u8, pooltype, pooltag);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void libdrv::FreeUnicodeString(_Inout_ UNICODE_STRING &s, _In_ ULONG pooltag)
{
        if (auto &b = s.Buffer) {
                ExFreePoolWithTag(b, pooltag);
                b = nullptr;
                s.Length = 0;
                s.MaximumLength = 0;
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED int libdrv::strchr(_In_ const UNICODE_STRING &s, _In_ WCHAR ch)
{
        PAGED_CODE();

        if (!s.Buffer) {
                return -1;
        }

        for (int i = 0; i < s.Length/sizeof(*s.Buffer); ++i) {
                if (s.Buffer[i] == ch) {
                        return i;
                }
        }

        return -1;
}

/*
 * Do not set .Buffer to NULL, RTL functions will return STATUS_INVALID_PARAMETER
 * instead of treating it as an empty string.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void libdrv::split(
        _Out_ UNICODE_STRING &head, _Out_ UNICODE_STRING &tail, 
        _In_ const UNICODE_STRING &str, _In_ WCHAR sep)
{
        PAGED_CODE();
        NT_ASSERT(&head != &tail);

        auto pos = strchr(str, sep);
        if (pos < 0) {
                head = str;
                tail.Length = 0;
                tail.MaximumLength = 0;
                tail.Buffer = str.Buffer;
                return;
        }

        auto s = str.Buffer;
        auto len = str.Length;

        head.Length = static_cast<USHORT>(pos)*sizeof(*s);
        head.MaximumLength = head.Length;
        head.Buffer = s;

        tail.Length = len - (head.Length + sizeof(*s));
        tail.MaximumLength = tail.Length;
        tail.Buffer = s + pos + 1;
}
