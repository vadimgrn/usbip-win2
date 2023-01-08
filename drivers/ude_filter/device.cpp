/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "driver.h"

namespace
{

using namespace usbip;

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto init(_Inout_ filter_ext &f, _In_opt_ filter_ext *parent)
{
	PAGED_CODE();
	NT_ASSERT(f.is_hub == !parent);

	IoInitializeRemoveLock(&f.remove_lock, pooltag, 0, 0);

	if (f.is_hub) {
		//
	} else if (auto lck = &parent->remove_lock; auto err = IoAcquireRemoveLock(lck, 0)) {
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return err;
	} else {
		f.dev.parent_remove_lock = lck;
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void do_destroy(_Inout_ filter_ext &f)
{
	PAGED_CODE();

	if (f.is_hub) {
		auto &hub = f.hub;
		if (auto ptr = hub.previous) {
			ExFreePoolWithTag(ptr, pooltag);
		}
	} else {
		auto &dev = f.dev;
		NT_ASSERT(!dev.usbd); // @see IRP_MN_REMOVE_DEVICE

		if (auto lck = dev.parent_remove_lock) {
			IoReleaseRemoveLock(lck, 0);
		}
	}
}

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

	TraceDbg("'%!USTR!'", &info->Name);
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

	DECLARE_CONST_UNICODE_STRING(name, L"\\Driver\\usbip2_vhci"); // FIXME: declare in header?
	return driver_name_equal(pdo->DriverObject, name, true);
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void usbip::destroy(_Inout_ filter_ext &f)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(f.self));

	if (auto &target = f.target) {
		IoDetachDevice(target);
		target = nullptr;
	}

	do_destroy(f);
	IoDeleteDevice(f.self);
}

/*
 * We're propagating a few Flags bits, DeviceType and Characteristics from the device object next beneath us.
 * We need to make these copies because the I/O Manager bases some of its decisions on what it sees 
 * in the topmost device object. 
 * 
 * In particular, whether a read or write IRP gets a memory descriptor list (MDL) 
 * or a system copy buffer depends on what the top object's DO_DIRECT_IO and DO_BUFFERED_IO flags are.
 * 
 * We don't need to copy the SectorSize or AlignmentRequirement members of the lower device object — 
 * IoAttachDeviceToDeviceStack will do that automatically.
 *
 * There's ordinarily no need for a filter device object (FiDO) to have its own name. 
 * If the function driver names its device object and creates a symbolic link, or if the function driver 
 * registers a device interface for its device object, an application will be able to open a handle 
 * for the device. Every IRP sent to the device gets sent first to the topmost FiDO driver, 
 * whether or not that FiDO has its own name.
 *
 * Do not use the FILE_DEVICE_SECURE_OPEN characteristics flag when you create a FiDO object. 
 * The PnP Manager propagates this flag, and a few others, up and down the device object stack. 
 * It's not your decision whether to enforce security checking on file opens - 
 * it's the function driver's and maybe the bus driver's.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(__yes))
PAGED NTSTATUS usbip::do_add_device(
	_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *pdo, _In_opt_ filter_ext *parent)
{
	PAGED_CODE();

	filter_ext *fltr{};
	DEVICE_OBJECT *fido{}; // Filter Device Object

	if (auto err = IoCreateDevice(drvobj, sizeof(*fltr), nullptr, FILE_DEVICE_UNKNOWN, 0, false, &fido)) {
		Trace(TRACE_LEVEL_ERROR, "IoCreateDevice %!STATUS!", err);
		return err;
	}

	fltr = get_filter_ext(fido); 

	fltr->self = fido;
	fltr->pdo = pdo;
	fltr->is_hub = !parent;

	if (auto err = init(*fltr, parent)) {
		destroy(*fltr);
		return err;
	}

	auto &target = fltr->target; // FDO or another FiDO

	target = IoAttachDeviceToDeviceStack(fido, pdo); // object to which fido was attached
	if (!target) {
		auto err = STATUS_NO_SUCH_DEVICE;
		Trace(TRACE_LEVEL_ERROR, "IoAttachDeviceToDeviceStack %!STATUS!", err);
		destroy(*fltr);
		return err;
	}

	fido->DeviceType = target->DeviceType;
	fido->Characteristics = target->Characteristics; 
	fido->Flags |= target->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE | DO_POWER_INRUSH);

	if (fltr->is_hub) {
		//
	} else if (auto &dev = fltr->dev;
		   auto err = USBD_CreateHandle(fido, target, USBD_CLIENT_CONTRACT_VERSION_602, pooltag, &dev.usbd)) {
		Trace(TRACE_LEVEL_ERROR, "USBD_CreateHandle %!STATUS!", err);
		destroy(*fltr);
		return err;
	}

	Trace(TRACE_LEVEL_INFORMATION, "FiDO %04x, pdo %04x (DeviceType %#lx), target %04x (DeviceType %#lx)", 
		ptr04x(fido), ptr04x(pdo), pdo->DeviceType, ptr04x(target), target->DeviceType);

	fido->Flags &= ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;
}

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS usbip::add_device(_In_ DRIVER_OBJECT *drvobj, _In_ DEVICE_OBJECT *hub_or_hci_pdo)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "drv %04x, pdo %04x", ptr04x(drvobj), ptr04x(hub_or_hci_pdo));

	if (!is_abobe_vhci(hub_or_hci_pdo)) {
		TraceDbg("Skip this device");
		return STATUS_SUCCESS;
	}

	return do_add_device(drvobj, hub_or_hci_pdo, nullptr);
}
