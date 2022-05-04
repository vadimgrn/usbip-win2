#include "strutil.h"
#include <ntstrsafe.h>

namespace
{

const ULONG libdrv_pooltag = 'vrdl';

inline auto RtlStringCchLength(PCSTR  s, size_t *len) { return RtlStringCchLengthA(s, NTSTRSAFE_MAX_CCH, len); }
inline auto RtlStringCchLength(PCWSTR s, size_t *len) { return RtlStringCchLengthW(s, NTSTRSAFE_MAX_CCH, len); }

template<typename T>
inline T *strdup(POOL_FLAGS Flags, const T *str)
{
        size_t len = 0;
        auto st = RtlStringCchLength(str, &len);
        if (st != STATUS_SUCCESS) {
                return nullptr;
        }

        auto sz = ++len*sizeof(*str);
        Flags |= POOL_FLAG_UNINITIALIZED;

        auto s = (T*)ExAllocatePool2(Flags, sz, libdrv_pooltag);
        if (s) {
                RtlCopyMemory(s, str, sz);
        }

        return s;
}

} // namespace


LPSTR libdrv_strdup(POOL_FLAGS Flags, LPCSTR str)
{
        return strdup(Flags, str);
}

LPWSTR libdrv_strdup(POOL_FLAGS Flags, LPCWSTR str)
{
        return strdup(Flags, str);
}

void libdrv_free(void *data)
{
	if (data) {
		ExFreePoolWithTag(data, libdrv_pooltag);
	}
}
