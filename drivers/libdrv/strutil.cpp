/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "strutil.h"

/*
 * RtlFreeUnicodeString must be used to release memory.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::utf8_to_unicode(_Out_ UNICODE_STRING &dst, _In_ const char *utf8)
{
        PAGED_CODE();

        UTF8_STRING s;
        RtlInitUTF8String(&s, utf8);

        return RtlUTF8StringToUnicodeString(&dst, &s, true);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::unicode_to_utf8(_Out_ char *dest, _In_ USHORT len, _In_ const UNICODE_STRING &src)
{
        PAGED_CODE();
        UTF8_STRING s{ .MaximumLength = len, .Buffer = dest };
        return RtlUnicodeStringToUTF8String(&s, &src, false);
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
