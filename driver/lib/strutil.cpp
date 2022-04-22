#include "strutil.h"
#include <ntstrsafe.h>

const ULONG libdrv_pooltag = 'vrdl';

LPWSTR libdrv_strdup(POOL_FLAGS Flags, LPCWSTR str)
{
	size_t len = 0;
	NTSTATUS st = RtlStringCchLengthW(str, NTSTRSAFE_MAX_CCH, &len);
	if (st != STATUS_SUCCESS) {
		return nullptr;
	}

	size_t sz = ++len*sizeof(*str);
        Flags |= POOL_FLAG_UNINITIALIZED;

        auto s = (LPWSTR)ExAllocatePool2(Flags, sz, libdrv_pooltag);
	if (s) {
		RtlCopyMemory(s, str, sz);
	}

	return s;
}

void libdrv_free(void *data)
{
	if (data) {
		ExFreePoolWithTag(data, libdrv_pooltag);
	}
}
