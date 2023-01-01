/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "driver.h"

namespace
{

using namespace usbip;

/*
 * DRIVER_OBJECT.DriverName is an undocumented member and can't be used.
 */
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED bool driver_name_equal(
	_In_ DRIVER_OBJECT *driver, _In_ const UNICODE_STRING &expected, _In_ bool CaseInSensitive)
{
	PAGED_CODE();

	buffer buf(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, 1024);
	if (!buf) {
		Trace(TRACE_LEVEL_ERROR, "Cannot allocate %Iu bytes", buf.size());
		return false;
	}

	auto info = buf.get<OBJECT_NAME_INFORMATION>();

	ULONG actual_sz;
	if (auto err = ObQueryNameString(driver, info, ULONG(buf.size()), &actual_sz)) {
		Trace(TRACE_LEVEL_ERROR, "ObQueryNameString %!STATUS!", err);
		return false;
	}

	TraceDbg("DriverName '%!USTR!'", &info->Name);
	return RtlEqualUnicodeString(&info->Name, &expected, CaseInSensitive);
}

/*
 * Do not check that HardwareID is "USB\ROOT_HUB30" because above usbip2_vhci can be nothing else.
 * 
 * for (auto cur = IoGetAttachedDeviceReference(pdo); cur; ) { // @see IoGetDeviceAttachmentBaseRef
 * 	auto lower = IoGetLowerDeviceObject(cur);
 *	ObDereferenceObject(cur);
 *	cur = lower;
 * }
 */
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto is_abobe_vhci(_In_ DEVICE_OBJECT *pdo)
{
	PAGED_CODE();

	DECLARE_CONST_UNICODE_STRING(vhci, L"\\Driver\\usbip2_vhci"); // FIXME: declare in header?
	return driver_name_equal(pdo->DriverObject, vhci, true);
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void* usbip::GetDeviceProperty(
	_In_ DEVICE_OBJECT *devobj, _In_ DEVICE_REGISTRY_PROPERTY prop, 
	_Inout_ NTSTATUS &error, _Inout_ ULONG &ResultLength)
{
	PAGED_CODE();

	if (!ResultLength) {
		ResultLength = 1024;
	}
	
	auto alloc = [] (auto len) { return ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, len, pooltag); };

	for (auto buf = alloc(ResultLength); buf; ) {

		error = IoGetDeviceProperty(devobj, prop, ResultLength, buf, &ResultLength);

		switch (error) {
		case STATUS_SUCCESS:
			return buf;
		case STATUS_BUFFER_TOO_SMALL:
			ExFreePoolWithTag(buf, pooltag);
			buf = alloc(ResultLength);
			break;
		default:
			TraceDbg("%!DEVICE_REGISTRY_PROPERTY! %!STATUS!", prop, error);
			ExFreePoolWithTag(buf, pooltag);
			return nullptr;
		}
	}

	error = STATUS_INSUFFICIENT_RESOURCES;
	return nullptr;
}

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(__yes))
PAGED NTSTATUS usbip::add_device(_In_ DRIVER_OBJECT*, _In_ DEVICE_OBJECT *pdo)
{
        PAGED_CODE();

	if (!is_abobe_vhci(pdo)) {
		TraceDbg("Skip this device");
		return STATUS_SUCCESS;
	}
	
	Trace(TRACE_LEVEL_INFORMATION, "pdo %04x", ptr04x(pdo));
	return STATUS_SUCCESS;
}
