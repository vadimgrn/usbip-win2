/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#include "wdm_cpp.h"

_IRQL_requires_(PASSIVE_LEVEL)
PAGED void *wdm::GetDeviceProperty(
	_In_ DEVICE_OBJECT *obj, _In_ DEVICE_REGISTRY_PROPERTY prop, _Out_ NTSTATUS &error, _Inout_ ULONG &ResultLength)
{
	PAGED_CODE();

	if (!ResultLength) {
		ResultLength = 255;
	}

	auto alloc = [] (auto len) { return ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, len, libdrv::pooltag); };

	for (auto buf = alloc(ResultLength); buf; ) {

		error = IoGetDeviceProperty(obj, prop, ResultLength, buf, &ResultLength);

		switch (error) {
		case STATUS_SUCCESS:
			return buf;
		case STATUS_BUFFER_TOO_SMALL:
			ExFreePoolWithTag(buf, libdrv::pooltag);
			buf = alloc(ResultLength);
			break;
		default:
			ExFreePoolWithTag(buf, libdrv::pooltag);
			return nullptr;
		}
	}

	error = STATUS_INSUFFICIENT_RESOURCES;
	return nullptr;
}
