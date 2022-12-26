/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "strutil.h"
#include "buffer.h"

#include <ntstrsafe.h>

namespace
{

const ULONG pooltag = 'VRDL';

inline auto RtlStringCchLength(PCSTR  s, size_t *len) { return RtlStringCchLengthA(s, NTSTRSAFE_MAX_CCH, len); }
inline auto RtlStringCchLength(PCWSTR s, size_t *len) { return RtlStringCchLengthW(s, NTSTRSAFE_MAX_CCH, len); }

template<typename T>
inline T *do_strdup(POOL_FLAGS Flags, const T *str)
{
        size_t len = 0;
        auto st = RtlStringCchLength(str, &len);
        if (st != STATUS_SUCCESS) {
                return nullptr;
        }

        auto sz = ++len*sizeof(*str);
        Flags |= POOL_FLAG_UNINITIALIZED;

        auto s = (T*)ExAllocatePool2(Flags, sz, pooltag);
        if (s) {
                RtlCopyMemory(s, str, sz);
        }

        return s;
}

} // namespace


LPSTR libdrv::strdup(POOL_FLAGS Flags, LPCSTR str)
{
        return do_strdup(Flags, str);
}

LPWSTR libdrv::strdup(POOL_FLAGS Flags, LPCWSTR str)
{
        return do_strdup(Flags, str);
}

void libdrv::free(void *data)
{
	if (data) {
		ExFreePoolWithTag(data, pooltag);
	}
}

/*
* RtlFreeUnicodeString must be used to release memory.
* @see RtlUTF8StringToUnicodeString
*/
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::to_unicode_str(_Out_ UNICODE_STRING &dst, _In_ const char *ansi)
{
        PAGED_CODE();

        ANSI_STRING s;
        RtlInitAnsiString(&s, ansi);

        return RtlAnsiStringToUnicodeString(&dst, &s, true);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::to_ansi_str(_Out_ char *dest, _In_ USHORT len, _In_ const UNICODE_STRING &src)
{
        PAGED_CODE();
        ANSI_STRING s{ 0, len, dest };
        return RtlUnicodeStringToAnsiString(&s, &src, false);
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED USHORT libdrv::strrchr(_In_ const UNICODE_STRING &s, _In_ WCHAR ch)
{
        PAGED_CODE();

        LONG cch = s.Length/sizeof(ch);

        for (auto i = cch - 1; i >= 0; --i) {
                if (s.Buffer[i] == ch) {
                        return USHORT(i + 1);
                }
        }

        return 0;
}

