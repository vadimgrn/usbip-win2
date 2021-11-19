#include "strutil.h"
#include <ntstrsafe.h>

ULONG libdrv_pooltag = 'dbil';

LPWSTR libdrv_strdupW(LPCWSTR str)
{
	size_t len = 0;
	NTSTATUS st = RtlStringCchLengthW(str, NTSTRSAFE_MAX_CCH, &len);
	if (st != STATUS_SUCCESS) {
		return NULL;
	}

	size_t sz = ++len*sizeof(*str);

	LPWSTR s = ExAllocatePoolWithTag(PagedPool, sz, libdrv_pooltag);
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

int libdrv_snprintf(char *buf, int size, const char *fmt, ...)
{
	va_list	arglist;
	size_t	len;
	NTSTATUS	status;

	va_start(arglist, fmt);
	status = RtlStringCchVPrintfA(buf, size, fmt, arglist);
	va_end(arglist);

	if (NT_ERROR(status))
		return 0;
	status = RtlStringCchLengthA(buf, size, &len);
	if (NT_ERROR(status))
		return 0;
	return (int)len;
}

int libdrv_snprintfW(PWCHAR buf, int size, LPCWSTR fmt, ...)
{
	va_list	arglist;
	size_t	len;
	NTSTATUS	status;

	va_start(arglist, fmt);
	status = RtlStringCchVPrintfW(buf, size, fmt, arglist);
	va_end(arglist);

	if (NT_ERROR(status))
		return 0;
	status = RtlStringCchLengthW(buf, size, &len);
	if (NT_ERROR(status))
		return 0;
	return (int)len;
}
