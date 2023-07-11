/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "strconv.h"

/*
 * @see RtlUnicodeStringToUTF8String
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::unicode_to_utf8(_Out_ UTF8_STRING &dst, _In_ const UNICODE_STRING &src)
{
        PAGED_CODE();

        ULONG actual{};
        auto st = RtlUnicodeToUTF8N(dst.Buffer, dst.MaximumLength, &actual, src.Buffer, src.Length);

        dst.Length = static_cast<USHORT>(actual);
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::unicode_to_utf8(_Out_ char *utf8, _In_ USHORT maxlen, _In_ const UNICODE_STRING &src)
{
        PAGED_CODE();

        if (!utf8) {
                return STATUS_INVALID_PARAMETER_1;
        }

        UTF8_STRING s { .MaximumLength = maxlen, .Buffer = utf8 };
        auto st = unicode_to_utf8(s, src);

        if (s.Length < s.MaximumLength) {
                s.Buffer[s.Length] = '\0'; // null-terminated
        }

        return st;
}

/*
 * @return libdrv::FreeUnicodeString must be used to release a memory
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

        ULONG dst_bytes{};
        if (auto st = RtlUTF8ToUnicodeN(nullptr, 0, &dst_bytes, src.Buffer, src.Length); NT_ERROR(st)) {
                return st;
        }

        dst.Buffer = (WCHAR*)ExAllocatePoolUninitialized(pooltype, dst_bytes, pooltag);
        if (!dst.Buffer) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        ULONG actual{};
        auto st = RtlUTF8ToUnicodeN(dst.Buffer, dst_bytes, &actual, src.Buffer, src.Length);

        NT_ASSERT(NT_SUCCESS(st));
        NT_ASSERT(actual == dst_bytes);
        
        dst.Length = static_cast<USHORT>(actual);
        dst.MaximumLength = static_cast<USHORT>(dst_bytes);

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

        if (!utf8) {
                return STATUS_INVALID_PARAMETER_2;
        }

        UTF8_STRING u8 {
                .Length = static_cast<USHORT>(strnlen(utf8, maxlen)),
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
PAGED int libdrv::strchr(_In_ const UNICODE_STRING &s, _In_ WCHAR ch, _In_ int from)
{
        PAGED_CODE();

        for (auto i = from; i < s.Length; ++i) {
                if (s.Buffer[i] == ch) {
                        return i;
                }
        }

        return -1;
}

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
                tail = UNICODE_STRING{};
                return;
        }

        auto s = str.Buffer;
        auto len = str.Length;

        head.Length = USHORT(pos)*sizeof(*s);
        head.MaximumLength = head.Length;
        head.Buffer = s;

        tail.Length = len - (head.Length + sizeof(*s));
        tail.MaximumLength = tail.Length;
        tail.Buffer = tail.Length ? ++pos + s : nullptr;
}
