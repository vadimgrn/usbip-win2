/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "strconv.h"

/*
 * @param utf8 null-terminated utf8 string
 * @param maxlen maximum size in bytes of the buffer to which "utf8" points 
          (this is not a length/size of "utf8" itself)
 * @return RtlFreeUnicodeString must be used to release a memory
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::utf8_to_unicode(_Out_ UNICODE_STRING &dst, _In_ const char *utf8, _In_ USHORT maxlen)
{
        PAGED_CODE();

        if (!utf8) {
                return STATUS_INVALID_PARAMETER_2;
        }

        UTF8_STRING src { // @see RtlInitUTF8StringEx
                .Length = static_cast<USHORT>(strnlen(utf8, maxlen)), // bytes
                .MaximumLength = maxlen,
                .Buffer = const_cast<char*>(utf8)
        };

        return RtlUTF8StringToUnicodeString(&dst, &src, true);
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
        auto st = RtlUnicodeStringToUTF8String(&s, &src, false);

        if (s.Length < s.MaximumLength) {
                s.Buffer[s.Length] = '\0'; // null-terminated
        }

        return st;
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
